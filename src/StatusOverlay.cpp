#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <string>
#include "StatusOverlay.hpp"
namespace sc6 {
namespace {
std::mutex stats_mutex;
OverlayStats latest;
ULONGLONG updated{};
constexpr COLORREF key=RGB(1,2,3);
struct View {HWND game{};HFONT font{},secondary_font{};unsigned dpi{};int width{},height{};};
BOOL CALLBACK find_game(HWND w,LPARAM data){
    DWORD pid{};GetWindowThreadProcessId(w,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w,GW_OWNER) ||
       (GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
    RECT r{};GetClientRect(w,&r);
    auto* view=reinterpret_cast<View*>(data);
    RECT best{};if(view->game)GetClientRect(view->game,&best);
    if((r.right-r.left)*(r.bottom-r.top)>(best.right-best.left)*(best.bottom-best.top))view->game=w;
    return TRUE;
}
std::wstring value(const std::optional<unsigned>& v,const wchar_t* unit){
    return v?std::to_wstring(*v)+unit:L"--";
}
void paint(HWND w,View& v){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);
    HDC memory=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
    auto old_bitmap=SelectObject(memory,bitmap);
    HBRUSH background=CreateSolidBrush(key);FillRect(memory,&r,background);DeleteObject(background);
    HBRUSH panel=CreateSolidBrush(RGB(18,22,29));auto old_brush=SelectObject(memory,panel);
    auto old_pen=SelectObject(memory,GetStockObject(NULL_PEN));
    RoundRect(memory,0,0,r.right,r.bottom,16,16);
    SelectObject(memory,old_brush);SelectObject(memory,old_pen);DeleteObject(panel);
    OverlayStats stats;ULONGLONG stamp{};{std::lock_guard lock(stats_mutex);stats=latest;stamp=updated;}
    bool stale=stats.connected && GetTickCount64()-stamp>3000;
    if(!stats.connected || stale){stats.ping_ms.reset();stats.delay_frames.reset();stats.rollback_frames.reset();}
    SetBkMode(memory,TRANSPARENT);auto old_font=SelectObject(memory,v.font);
    SetTextColor(memory,RGB(109,231,176));
    int pad=MulDiv(16,v.dpi,96),top=MulDiv(10,v.dpi,96);
    RECT label{pad,top,r.right-pad,top+MulDiv(22,v.dpi,96)};
    DrawTextW(memory,L"SC6Rollback  ENABLED",-1,&label,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(memory,v.secondary_font);SetTextColor(memory,RGB(156,166,182));
    RECT subtitle{pad,top+MulDiv(24,v.dpi,96),r.right-pad,r.bottom};
    DrawTextW(memory,stale?L"Network: telemetry stale":stats.connected?L"Network: peer connected":L"Network: inactive",-1,&subtitle,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    int start=MulDiv(255,v.dpi,96),column=(r.right-pad-start)/3;
    const std::wstring texts[]={L"PING",L"DELAY",L"ROLLBACK"};
    const std::wstring values[]={value(stats.ping_ms,L" ms"),value(stats.delay_frames,L" f"),value(stats.rollback_frames,L" f")};
    for(int i=0;i<3;++i){RECT cell{start+i*column,top,start+(i+1)*column,top+MulDiv(20,v.dpi,96)};
        SelectObject(memory,v.secondary_font);SetTextColor(memory,RGB(156,166,182));DrawTextW(memory,texts[i].c_str(),-1,&cell,DT_CENTER|DT_SINGLELINE);
        cell.top+=MulDiv(23,v.dpi,96);cell.bottom=r.bottom;
        SelectObject(memory,v.font);SetTextColor(memory,RGB(237,241,248));DrawTextW(memory,values[i].c_str(),-1,&cell,DT_CENTER|DT_SINGLELINE);
    }
    SelectObject(memory,old_font);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);
    SelectObject(memory,old_bitmap);DeleteObject(bitmap);DeleteDC(memory);EndPaint(w,&ps);
}
void position(HWND w,View& v){
    if(!v.game || !IsWindow(v.game)){v.game=nullptr;EnumWindows(find_game,reinterpret_cast<LPARAM>(&v));}
    if(!v.game || GetForegroundWindow()!=v.game || IsIconic(v.game)){ShowWindow(w,SW_HIDE);return;}
    RECT client{};GetClientRect(v.game,&client);POINT origin{};ClientToScreen(v.game,&origin);
    unsigned dpi=GetDpiForWindow(v.game);if(!dpi)dpi=96;
    // Fit the complete four-column layout even in a secondary_fonter game window.
    dpi=std::min(dpi,static_cast<unsigned>(std::max(64L,client.right*96/660)));
    if(v.dpi!=dpi){
        if(v.font)DeleteObject(v.font);if(v.secondary_font)DeleteObject(v.secondary_font);v.dpi=dpi;
        v.font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        v.secondary_font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    v.width=MulDiv(640,dpi,96);v.height=MulDiv(66,dpi,96);
    SetWindowPos(w,HWND_TOPMOST,origin.x+(client.right-v.width)/2,origin.y+client.bottom-v.height-MulDiv(16,dpi,96),v.width,v.height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(w,nullptr,FALSE);
}
LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM wp,LPARAM lp){
    auto* view=reinterpret_cast<View*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(message==WM_NCCREATE){view=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));}
    switch(message){
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:if(view){paint(w,*view);return 0;}break;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,message,wp,lp);
}
}
void StatusOverlay::publish(const OverlayStats& stats){std::lock_guard lock(stats_mutex);latest=stats;updated=GetTickCount64();}
void StatusOverlay::start(){if(worker.joinable())return;stopping=false;worker=std::thread([this]{run();});}
void StatusOverlay::stop(){stopping=true;if(worker.joinable())worker.join();}
void StatusOverlay::run(){
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&module);
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=module;wc.lpszClassName=L"SC6RollbackStatusOverlay";
    if(!RegisterClassW(&wc))return;
    View view;
    HWND window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"SC6Rollback status",WS_POPUP,0,0,1,1,nullptr,nullptr,module,&view);
    if(window){
        SetLayeredWindowAttributes(window,key,235,LWA_COLORKEY|LWA_ALPHA);
        MSG msg{};bool quit=false;
        while(!stopping && !quit){
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){quit=true;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(!quit){position(window,view);MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);}
        }
        if(IsWindow(window))DestroyWindow(window);
    }
    if(view.font)DeleteObject(view.font);if(view.secondary_font)DeleteObject(view.secondary_font);
    UnregisterClassW(wc.lpszClassName,module);
}
}
