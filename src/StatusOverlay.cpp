#define NOMINMAX
#include <Windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>
#include "StatusOverlay.hpp"
#include "UpdateClient.hpp"
namespace sc6 {
namespace {
std::mutex stats_mutex;
OverlayStats latest;
ULONGLONG updated{};

struct View {HWND game{};HFONT font{},secondary_font{};unsigned dpi{};int width{},height{};bool hovered{},pressed{},checking{};updater::Client updater;};
RECT update_button(const View& v){return {MulDiv(465,v.dpi,96),MulDiv(10,v.dpi,96),MulDiv(684,v.dpi,96),MulDiv(32,v.dpi,96)};}
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
    HDC memory=CreateCompatibleDC(dc);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!memory || !bitmap || !pixels){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);EndPaint(w,&ps);return;}
    auto old_bitmap=SelectObject(memory,bitmap);
    {
    Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics graphics(&surface);
    graphics.Clear(Gdiplus::Color(0,0,0,0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::GraphicsPath shape;const float diameter=16.0f;
    shape.AddArc(0.0f,0.0f,diameter,diameter,180.0f,90.0f);
    shape.AddArc(r.right-diameter,0.0f,diameter,diameter,270.0f,90.0f);
    shape.AddArc(r.right-diameter,r.bottom-diameter,diameter,diameter,0.0f,90.0f);
    shape.AddArc(0.0f,r.bottom-diameter,diameter,diameter,90.0f,90.0f);shape.CloseFigure();
    Gdiplus::SolidBrush panel(Gdiplus::Color(128,0,0,0));graphics.FillPath(&panel,&shape);
    Gdiplus::Font primary(memory,v.font),secondary(memory,v.secondary_font);
    auto draw=[&](const wchar_t* text,const RECT& bounds,bool large,COLORREF color,bool center=false){
        Gdiplus::SolidBrush brush(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));
        Gdiplus::StringFormat format;format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        format.SetAlignment(center?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);
        Gdiplus::RectF box(float(bounds.left),float(bounds.top),float(bounds.right-bounds.left),float(bounds.bottom-bounds.top));
        graphics.DrawString(text,-1,large?&primary:&secondary,box,&format,&brush);
    };
    OverlayStats stats;ULONGLONG stamp{};{std::lock_guard lock(stats_mutex);stats=latest;stamp=updated;}
    bool stale=stats.connected && GetTickCount64()-stamp>3000;
    if(!stats.connected || stale){stats.ping_ms.reset();stats.delay_frames.reset();stats.rollback_frames.reset();}
    int pad=MulDiv(16,v.dpi,96),top=MulDiv(10,v.dpi,96);
    RECT label{pad,top,MulDiv(245,v.dpi,96),top+MulDiv(22,v.dpi,96)};
    draw(stats.connected?L"SC6: Astral  ENABLED":L"SC6: Astral  DISABLED",label,true,
         stats.connected?RGB(109,231,176):RGB(255,100,100));
    RECT cell{MulDiv(255,v.dpi,96),top,MulDiv(355,v.dpi,96),top+MulDiv(20,v.dpi,96)};
    draw(L"PING ~",cell,false,RGB(156,166,182),true);
    cell.top+=MulDiv(23,v.dpi,96);cell.bottom=r.bottom;
    draw(value(stats.ping_ms,L" ms").c_str(),cell,true,RGB(237,241,248),true);
    RECT delay_cell{MulDiv(365,v.dpi,96),top,MulDiv(445,v.dpi,96),top+MulDiv(20,v.dpi,96)};
    draw(L"DELAY",delay_cell,false,RGB(156,166,182),true);
    delay_cell.top+=MulDiv(23,v.dpi,96);delay_cell.bottom=r.bottom;
    draw(value(stats.delay_frames,L"f").c_str(),delay_cell,true,RGB(237,241,248),true);
    RECT version{pad,MulDiv(36,v.dpi,96),MulDiv(245,v.dpi,96),MulDiv(56,v.dpi,96)};
    draw((std::wstring(L"Build ")+updater::version).c_str(),version,false,RGB(156,166,182));
    RECT update_status{MulDiv(465,v.dpi,96),MulDiv(36,v.dpi,96),MulDiv(684,v.dpi,96),MulDiv(56,v.dpi,96)};
    const auto status=v.updater.status();
    v.checking=status==L"Checking for updates...";
    const bool ready=status==L"Update ready - installs after exit";
    const bool available=status==L"Downloading verified update..." || ready;
    const bool enabled=v.updater.can_check();
    const bool hover=enabled && v.hovered,pressed=enabled && v.pressed;
    draw(status.c_str(),update_status,false,RGB(156,166,182),true);
    const auto button=update_button(v);
    const float scale=float(v.dpi)/96.0f;
    Gdiplus::RectF button_box(float(button.left),float(button.top),float(button.right-button.left),float(button.bottom-button.top));
    Gdiplus::SolidBrush button_fill(Gdiplus::Color(pressed?230: hover?210:170,hover?55:30,hover?73:40,hover?88:50));
    Gdiplus::Pen border(Gdiplus::Color(enabled?255:140,hover?160:90,hover?210:115,hover?230:135),scale);
    graphics.FillRectangle(&button_fill,button_box);graphics.DrawRectangle(&border,button_box);
    RECT caption=button;caption.top+=MulDiv(3,v.dpi,96);
    if(v.checking){
        caption.left+=MulDiv(20,v.dpi,96);
        Gdiplus::Pen ring(Gdiplus::Color(255,109,231,176),2.0f*scale);
        graphics.DrawArc(&ring,float(button.left)+12*scale,float(button.top)+5*scale,12*scale,12*scale,
                         float(GetTickCount64()%1000)*0.36f,270.0f);
    }
    const auto cooldown=v.updater.cooldown_seconds();
    const std::wstring button_text=ready?L"Close game to update":available?L"Update available":v.checking?L"Checking...":
        cooldown?L"Check again in "+std::to_wstring(cooldown)+L"s":L"Check for updates";
    draw(button_text.c_str(),caption,false,
         available?RGB(255,100,100):cooldown && !v.checking?RGB(156,166,182):RGB(109,231,176),true);
    } // Finish GDI+ rendering before presenting the premultiplied pixels.
    POINT source{};SIZE size{r.right,r.bottom};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(w,dc,nullptr,&size,memory,&source,0,&blend,ULW_ALPHA);
    SelectObject(memory,old_bitmap);DeleteObject(bitmap);DeleteDC(memory);EndPaint(w,&ps);
}
void position(HWND w,View& v){
    if(!v.game || !IsWindow(v.game)){v.game=nullptr;EnumWindows(find_game,reinterpret_cast<LPARAM>(&v));}
    if(!v.game || GetForegroundWindow()!=v.game || IsIconic(v.game)){ShowWindow(w,SW_HIDE);return;}
    if(GetWindow(w,GW_OWNER)!=v.game){
        SetWindowLongPtrW(w,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(v.game));
    }
    RECT client{};GetClientRect(v.game,&client);POINT origin{};ClientToScreen(v.game,&origin);
    unsigned dpi=GetDpiForWindow(v.game);if(!dpi)dpi=96;
    // Fit all four columns while retaining the original two-row overlay height.
    dpi=std::min(dpi,static_cast<unsigned>(std::max(48L,client.right*96/720)));
    if(v.dpi!=dpi){
        if(v.font)DeleteObject(v.font);if(v.secondary_font)DeleteObject(v.secondary_font);v.dpi=dpi;
        v.font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        v.secondary_font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    v.width=MulDiv(700,dpi,96);v.height=MulDiv(66,dpi,96);
    SetWindowPos(w,HWND_TOPMOST,origin.x+(client.right-v.width)/2,origin.y+client.bottom-v.height-MulDiv(16,dpi,96),v.width,v.height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    // Retain click-through for the status panel. Only the update control takes mouse input.
    POINT cursor{};GetCursorPos(&cursor);ScreenToClient(w,&cursor);
    const auto button=update_button(v);const auto style=GetWindowLongPtrW(w,GWL_EXSTYLE);
    v.hovered=PtInRect(&button,cursor)!=FALSE;
    const auto desired=PtInRect(&button,cursor)?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT);
    if(style!=desired)SetWindowLongPtrW(w,GWL_EXSTYLE,desired);
    InvalidateRect(w,nullptr,FALSE);
}
LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM wp,LPARAM lp){
    auto* view=reinterpret_cast<View*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(message==WM_NCCREATE){view=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));}
    switch(message){
    case WM_NCHITTEST:if(view){POINT point{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};ScreenToClient(w,&point);const auto button=update_button(*view);if(PtInRect(&button,point))return HTCLIENT;}return HTTRANSPARENT;
    case WM_LBUTTONDOWN:if(view){if(view->updater.can_check()){view->pressed=true;SetCapture(w);InvalidateRect(w,nullptr,FALSE);}return 0;}break;
    case WM_CAPTURECHANGED:if(view){view->pressed=false;InvalidateRect(w,nullptr,FALSE);}break;
    case WM_LBUTTONUP:if(view){const bool pressed=view->pressed;view->pressed=false;ReleaseCapture();POINT point{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};const auto button=update_button(*view);if(pressed && PtInRect(&button,point)){view->updater.check();InvalidateRect(w,nullptr,FALSE);return 0;}}break;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(view && view->updater.can_check()?32649:32512)));return TRUE;}break;
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
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR graphics_token{};
    if(Gdiplus::GdiplusStartup(&graphics_token,&startup,nullptr)!=Gdiplus::Ok){UnregisterClassW(wc.lpszClassName,module);return;}
    View view;
    view.updater.start();
    HWND window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"SC6Rollback status",WS_POPUP,0,0,1,1,nullptr,nullptr,module,&view);
    if(window){
        MSG msg{};bool quit=false;
        while(!stopping && !quit){
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){quit=true;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(!quit){position(window,view);MsgWaitForMultipleObjects(0,nullptr,FALSE,view.checking?33:100,QS_ALLINPUT);}
        }
        if(IsWindow(window))DestroyWindow(window);
    }
    if(view.font)DeleteObject(view.font);if(view.secondary_font)DeleteObject(view.secondary_font);
    Gdiplus::GdiplusShutdown(graphics_token);
    UnregisterClassW(wc.lpszClassName,module);
}
}
