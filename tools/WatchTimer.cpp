// Windows x64 diagnostic: attach, watch 4-byte timer writes, capture stacks,
// restore debug registers and detach. No code patches or game-memory writes.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <set>

// Disposable target used only by --self-test. Two threads exercise both
// execution and data breakpoints. It exits normally after the parent signals.
static volatile LONG testTimer=0;
__declspec(noinline) void testRoutine() { InterlockedIncrement(&testTimer); }
__declspec(noinline) void testCaller() { testRoutine(); }
DWORD WINAPI testWorker(void* stop) {
    while (WaitForSingleObject(stop,1)==WAIT_TIMEOUT) testCaller();
    return 0;
}

struct Saved { DWORD64 dr0, dr1, dr2, dr6, dr7; };
static volatile LONG interrupted = 0;
BOOL WINAPI stop_handler(DWORD) { InterlockedExchange(&interrupted, 1); return TRUE; }

int wmain(int argc, wchar_t** argv) {
    if ((argc==3 || argc==4) && std::wstring(argv[1])==L"--test-target") {
        HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);
        if (!stop) return 3;
        if(argc==4) {WaitForSingleObject(stop,10000);CloseHandle(stop);return 0;}
        HANDLE worker=CreateThread(nullptr,0,testWorker,stop,0,nullptr);
        if (!worker) {CloseHandle(stop);return 3;}
        testWorker(stop);
        WaitForSingleObject(worker,2000);
        CloseHandle(worker);CloseHandle(stop);return 0;
    }
    const bool selfTest=argc==4 && std::wstring(argv[1])==L"--self-test";
    const std::wstring scenario=selfTest ? argv[2] : L"";
    if (selfTest && scenario!=L"limit" && scenario!=L"timeout" && scenario!=L"cancel" && scenario!=L"idle") return 2;
    if (!selfTest && ((argc != 4 && argc != 5) || std::wstring(argv[2]) != L"--training-confirmed")) {
        std::cerr << "Usage: WatchTimer.exe PID --training-confirmed output.txt [callers]\n";
        return 2;
    }
    DWORD pid = selfTest ? 0 : std::stoul(argv[1]);
    const bool outerTest = !selfTest && argc==5 && std::wstring(argv[4])==L"outer-smoke";
    const bool smokeTest = outerTest || (!selfTest && argc==5 && std::wstring(argv[4])==L"smoke");
    const bool callers = selfTest || smokeTest || (argc == 5 && std::wstring(argv[4]) == L"callers");
    if (!selfTest && argc == 5 && !callers) return 2;
    if (!selfTest && !smokeTest) {
        std::cerr << "Live game tracing remains disabled pending a reviewed game-validation plan. Use --self-test for isolated tests.\n";
        return 2;
    }
    const DWORD64 slotMask = callers ? 7 : 1;
    const DWORD64 registerMask = callers ? 0xFFF003F : 0xF0003;
    std::ofstream log(argv[3]);
    if (!log) return 2;
    PROCESS_INFORMATION child{};
    HANDLE testStop=nullptr;
    DEBUG_EVENT firstEvent{};
    bool firstPending=false;
    HANDLE process=nullptr;
    DWORD64 childBase=0;
    if (selfTest) {
        const auto eventName=L"Local\\SC6WatchTest_"+std::to_wstring(GetCurrentProcessId());
        testStop=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
        wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
        std::wstring command=L"\""+std::wstring(exe)+L"\" --test-target "+eventName;
        if(scenario==L"idle") command+=L" idle";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if (!testStop || !CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) {
            log << "test_launch_failed=" << GetLastError() << '\n';
            if(testStop) CloseHandle(testStop);return 1;
        }
        process=child.hProcess;pid=child.dwProcessId;
        if (!DebugSetProcessKillOnExit(FALSE) || !WaitForDebugEvent(&firstEvent,5000)) {
            // This is our disposable child, never SC6.
            TerminateProcess(process,4);CloseHandle(child.hThread);CloseHandle(process);CloseHandle(testStop);return 1;
        }
        firstPending=true;
        if(firstEvent.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT)
            childBase=reinterpret_cast<DWORD64>(firstEvent.u.CreateProcessInfo.lpBaseOfImage);
    } else process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) { std::cerr << "OpenProcess: " << GetLastError() << '\n'; return 1; }
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    DWORD64 base = childBase;
    if (modules != INVALID_HANDLE_VALUE) {
        if (Module32FirstW(modules, &module)) {
            do {
                if (_wcsicmp(module.szModule, L"SoulcaliburVI.exe") == 0)
                    base = reinterpret_cast<DWORD64>(module.modBaseAddr);
            } while (Module32NextW(modules, &module));
        }
        CloseHandle(modules);
    }
    if (!base) {
        HMODULE executable{}; DWORD needed{};
        if (EnumProcessModulesEx(process, &executable, sizeof(executable), &needed, LIST_MODULES_ALL))
            base=reinterpret_cast<DWORD64>(executable);
    }
    if (!base) { log << "module_lookup_failed=" << GetLastError() << '\n'; CloseHandle(process); return 2; }
    // Hash validation is performed by Watch-Timer.ps1 before this executable.
    const auto localBase=reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    const DWORD64 watched = base + (selfTest ? reinterpret_cast<DWORD64>(&testTimer)-localBase : (outerTest ? 0x470D0C4 : 0x47F1428));
    const DWORD64 routine=base+(selfTest ? reinterpret_cast<DWORD64>(&testRoutine)-localBase : (outerTest ? 0x2DBC60 : 0x2FFEB0));
    const DWORD64 caller=base+(selfTest ? reinterpret_cast<DWORD64>(&testCaller)-localBase : (outerTest ? 0x34DA70 : 0x2FDEA0));
    log << "module_base=0x" << std::hex << base << " timer=0x" << watched
        << " mode=" << (outerTest ? "outer-smoke" : (callers ? "callers" : "writes")) << '\n';
    if (!selfTest && !DebugActiveProcess(pid)) {
        log << "attach_failed=" << std::dec << GetLastError() << '\n';
        CloseHandle(process); return 1;
    }
    if (!DebugSetProcessKillOnExit(FALSE)) {
        DebugActiveProcessStop(pid); CloseHandle(process); return 1;
    }
    SetConsoleCtrlHandler(stop_handler, TRUE);
    const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
    std::map<DWORD, Saved> saved;
    bool failed = false, exited = false, initial_break_seen = false;
    unsigned hits = 0;
    unsigned routineHits=0, callerHits=0, writeHits=0;
    const auto start = std::chrono::steady_clock::now();
    auto arm = [&](DWORD tid) {
        if (saved.count(tid)) return;
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (!thread || !GetThreadContext(thread, &ctx) || (ctx.Dr7 & (callers ? 0x3F : 3))) {
            log << "Cannot safely arm thread " << std::dec << tid << '\n'; failed = true;
        } else {
            saved.emplace(tid, Saved{ctx.Dr0, ctx.Dr1, ctx.Dr2, ctx.Dr6, ctx.Dr7});
            ctx.Dr0 = callers ? routine : watched;
            if (callers) { ctx.Dr1=caller; ctx.Dr2=watched; }
            ctx.Dr6 &= ~slotMask;
            ctx.Dr7 = (ctx.Dr7 & ~registerMask) |
                (callers ? (DWORD64(0x15) | (DWORD64(0xD)<<24)) : (1 | (DWORD64(0xD)<<16)));
            if (!SetThreadContext(thread, &ctx)) failed = true;
        }
        if (thread) CloseHandle(thread);
    };
    bool stopping=false, stoppedAtEvent=false;
    bool requestedStopBreak=false;
    DEBUG_EVENT heldEvent{};DWORD heldStatus=DBG_CONTINUE;
    if(outerTest) log << "slot1=outer_entry slot2=player_entry slot4=outer_counter_write timer_field=outer_counter\n";
    const unsigned limit=selfTest ? (scenario==L"limit" ? 100u : 1000000u) : 100u;
    const auto timeout=std::chrono::milliseconds(selfTest ? ((scenario==L"timeout" || scenario==L"idle") ? 150 : 2000) : 2000);
    while (!exited) {
        const auto elapsed=std::chrono::steady_clock::now()-start;
        if (selfTest && scenario==L"cancel" && elapsed>=std::chrono::milliseconds(150))
            InterlockedExchange(&interrupted,1);
        stopping=stopping || failed || interrupted || hits>=limit || elapsed>=timeout;
        DEBUG_EVENT event{};
        bool stopBreakHandled=false;
        if(firstPending) {event=firstEvent;firstPending=false;}
        else if (!WaitForDebugEvent(&event, 100)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) {failed=true;break;}
            // Never resume-and-race into register cleanup. If idle, request a
            // debug stop and service its thread/events before detaching.
            if(stopping && !requestedStopBreak) {
                HANDLE breaker=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pid);
                const bool requested=breaker && DebugBreakProcess(breaker);
                if(breaker) CloseHandle(breaker);
                if(!requested) {log << "stop_request_failed=" << GetLastError() << '\n';failed=true;break;}
                requestedStopBreak=true;
            }
            continue;
        }
        DWORD status = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            arm(event.dwThreadId);
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            // Debug API owns these process/thread handles until exit/detach.
            // Closing them early risks handle-value reuse before an exit event.
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            arm(event.dwThreadId);
            break;
        case EXIT_THREAD_DEBUG_EVENT: saved.erase(event.dwThreadId); break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXIT_PROCESS_DEBUG_EVENT: exited = true; break;
        case EXCEPTION_DEBUG_EVENT: {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            status = DBG_EXCEPTION_NOT_HANDLED;
            if (code == EXCEPTION_BREAKPOINT && !initial_break_seen) {
                initial_break_seen = true; status = DBG_CONTINUE;
            } else if(code==EXCEPTION_BREAKPOINT && requestedStopBreak) {
                stopBreakHandled=true;status=DBG_CONTINUE;
            } else if (code == EXCEPTION_SINGLE_STEP) {
                HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId);
                CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_ALL;
                if (thread && GetThreadContext(thread, &ctx) && (ctx.Dr6 & slotMask) && saved.count(event.dwThreadId)) {
                    DWORD timer{}; SIZE_T read{};
                    const bool timerOk=ReadProcessMemory(process, reinterpret_cast<void*>(watched), &timer, sizeof(timer), &read) && read==sizeof(timer);
                    const bool execution=callers && (ctx.Dr6 & 3);
                    if (callers && (ctx.Dr6 & 1)) ++routineHits;
                    if (callers && (ctx.Dr6 & 2)) ++callerHits;
                    if (ctx.Dr6 & (callers ? 4 : 1)) ++writeHits;
                    const auto elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
                    log << "hit=" << std::dec << ++hits << " ms=" << elapsedMs << " tid=" << event.dwThreadId
                        << " timer=" << timer << " timer_ok=" << timerOk
                        << " slots=" << (ctx.Dr6 & slotMask)
                        << (execution ? " entry_rip_rva=0x" : " next_rip_rva=0x") << std::hex << (ctx.Rip-base) << '\n';
                    log << "regs rcx=" << ctx.Rcx << " rdx=" << ctx.Rdx << " rbx=" << ctx.Rbx
                        << " rsi=" << ctx.Rsi << " rdi=" << ctx.Rdi << " r8=" << ctx.R8 << " r9=" << ctx.R9 << '\n';
                    if (callers && !selfTest) {
                        DWORD p1{}; DWORD64 returnAddress{};
                        const bool p1Ok=ReadProcessMemory(process,reinterpret_cast<void*>(base+0x475A038),&p1,sizeof(p1),&read) && read==sizeof(p1);
                        const bool returnOk=execution && ReadProcessMemory(process,reinterpret_cast<void*>(ctx.Rsp),&returnAddress,sizeof(returnAddress),&read) && read==sizeof(returnAddress);
                        log << "p1_timer_candidate=" << std::dec << p1 << " p1_ok=" << p1Ok
                            << " return_ok=" << returnOk << " return_abs=0x" << std::hex << returnAddress << '\n';
                    }
                    if (symbols && (!callers || hits<=6)) {
                        CONTEXT walkContext = ctx;
                        STACKFRAME64 frame{};
                        frame.AddrPC = {ctx.Rip, 0, AddrModeFlat};
                        frame.AddrStack = {ctx.Rsp, 0, AddrModeFlat};
                        frame.AddrFrame = {ctx.Rbp, 0, AddrModeFlat};
                        for (int i=0; i<12; ++i) {
                            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &walkContext,
                                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                            if (!frame.AddrPC.Offset) break;
                            log << "  stack_abs=0x" << frame.AddrPC.Offset;
                            if (frame.AddrPC.Offset>=base && frame.AddrPC.Offset<base+0x4ba2000)
                                log << " game_rva=0x" << frame.AddrPC.Offset-base;
                            log << '\n';
                        }
                    }
                    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS | (execution ? CONTEXT_CONTROL : 0);
                    ctx.Dr6 &= ~slotMask;
                    // Execute breakpoints fault BEFORE the instruction. RF permits it
                    // to run once without immediately triggering the same breakpoint.
                    if (execution) ctx.EFlags |= 0x10000;
                    if (!SetThreadContext(thread, &ctx)) failed=true;
                    status = DBG_CONTINUE;
                }
                if (thread) CloseHandle(thread);
            }
            break;
        }
        }
        stopping=stopping || failed || interrupted || hits>=limit || std::chrono::steady_clock::now()-start>=timeout;
        if(stopping && !exited && (!requestedStopBreak || stopBreakHandled)) {
            heldEvent=event;heldStatus=status;stoppedAtEvent=true;break;
        }
        if (!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status)) {failed=true;break;}
    }
    // At a held debug event all target threads are stopped. Add a balanced
    // suspension so they cannot execute between ContinueDebugEvent and detach.
    // The old implementation resumed first, leaving active breakpoints racing
    // with cleanup. No successful cleanup is reported without a held stop.
    bool restored = exited || stoppedAtEvent;
    std::vector<HANDLE> suspended;
    std::set<DWORD> pendingSteps;
    if (!exited) for (const auto& [tid, original] : saved) {
        HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT, FALSE,tid);
        if (!thread) { if(GetLastError()!=ERROR_INVALID_PARAMETER) {log<<"cleanup_open_failed="<<GetLastError()<<'\n';restored=false;} continue; }
        if (SuspendThread(thread) != DWORD(-1)) {
            suspended.push_back(thread);
            CONTEXT ctx{};ctx.ContextFlags=CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread,&ctx)) {
                if(ctx.Dr6 & slotMask) pendingSteps.insert(tid);
                ctx.Dr0=original.dr0;ctx.Dr6=original.dr6;
                if (callers) {ctx.Dr1=original.dr1;ctx.Dr2=original.dr2;}
                ctx.Dr7=(ctx.Dr7 & ~registerMask) | (original.dr7 & registerMask);
                if (!SetThreadContext(thread,&ctx)) {log<<"cleanup_set_failed="<<GetLastError()<<'\n';restored=false;}
            } else {log<<"cleanup_get_failed="<<GetLastError()<<'\n';restored=false;}
        } else {log<<"cleanup_suspend_failed="<<GetLastError()<<'\n';restored=false;CloseHandle(thread);}
    }
    if (symbols) SymCleanup(process);
    if(stoppedAtEvent && !ContinueDebugEvent(heldEvent.dwProcessId,heldEvent.dwThreadId,heldStatus)) failed=true;
    // All owned slots are now disabled. Resume while STILL attached so any
    // already-pending thread exception is delivered to us, not to the game.
    for(HANDLE thread:suspended) {
        if(ResumeThread(thread)==DWORD(-1)) {
            log<<"cleanup_resume_failed="<<GetLastError()<<'\n';restored=false;
        }
        CloseHandle(thread);
    }
    suspended.clear();
    // Another thread may already have raised a debug exception when the held
    // event stopped the process. Drain those events with target threads still
    // running with owned slots disabled, before detaching.
    for(;;) {
        DEBUG_EVENT queued{};
        if(!WaitForDebugEvent(&queued,100)) {
            if(GetLastError()!=ERROR_SEM_TIMEOUT) failed=true;
            break;
        }
        DWORD disposition=DBG_CONTINUE;
        if(queued.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            disposition=DBG_EXCEPTION_NOT_HANDLED;
            if(queued.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP && pendingSteps.erase(queued.dwThreadId))
                disposition=DBG_CONTINUE;
        } else if(queued.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) exited=true;
        else if(queued.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && queued.u.LoadDll.hFile) CloseHandle(queued.u.LoadDll.hFile);
        else if(queued.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT) {
            // New threads are deliberately not armed during drain.
        }
        log<<"drained_event="<<queued.dwDebugEventCode<<" tid="<<queued.dwThreadId<<'\n';
        if(!ContinueDebugEvent(queued.dwProcessId,queued.dwThreadId,disposition)) {failed=true;break;}
        if(exited) break;
    }
    const bool detached = exited || DebugActiveProcessStop(pid);
    log << std::dec << "hits=" << hits << " restored=" << restored << " detached=" << detached
        << " failed=" << failed << " routine_calls=" << routineHits << " caller_calls=" << callerHits
        << " timer_writes=" << writeHits << '\n';
    bool survived=true;
    if(smokeTest) {
        DWORD before{},after{},exitCode{};SIZE_T n{};
        const bool readBefore=ReadProcessMemory(process,reinterpret_cast<void*>(watched),&before,sizeof(before),&n) && n==sizeof(before);
        Sleep(2000);
        const bool readAfter=ReadProcessMemory(process,reinterpret_cast<void*>(watched),&after,sizeof(after),&n) && n==sizeof(after);
        survived=GetExitCodeProcess(process,&exitCode) && exitCode==STILL_ACTIVE && readBefore && readAfter && after>before;
        log << "game_alive_and_timer_progressed=" << survived << " before=" << before << " after=" << after << '\n';
    }
    if(selfTest) {
        LONG before{},after{};SIZE_T n{};
        const bool readBefore=ReadProcessMemory(process,reinterpret_cast<void*>(watched),&before,sizeof(before),&n) && n==sizeof(before);
        Sleep(100);
        const bool readAfter=ReadProcessMemory(process,reinterpret_cast<void*>(watched),&after,sizeof(after),&n) && n==sizeof(after);
        survived=WaitForSingleObject(process,0)==WAIT_TIMEOUT && readBefore && readAfter && (scenario==L"idle" || after>before);
        const bool signaled=SetEvent(testStop)!=FALSE;
        const DWORD waitResult=WaitForSingleObject(process,3000);
        const bool finished=waitResult==WAIT_OBJECT_0;
        DWORD exitCode=STILL_ACTIVE;GetExitCodeProcess(process,&exitCode);
        log << "test_survived=" << survived << " progressed=" << (after>before)
            << " exited_normally=" << (finished && exitCode==0)
            << " signaled=" << signaled << " wait=" << waitResult << " exit=" << exitCode << '\n';
        survived=survived && finished && exitCode==0;
        if(!finished) TerminateProcess(process,5);
        CloseHandle(child.hThread);CloseHandle(testStop);
    }
    CloseHandle(process);
    return !failed && restored && detached && (hits>0 || (selfTest && scenario==L"idle")) && survived ? 0 : 1;
}


