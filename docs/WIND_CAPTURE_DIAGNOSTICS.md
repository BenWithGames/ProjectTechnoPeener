# Wind capture failure 35696

The report identifies session failure 2 after 51 captured and retired bindings, but the previous build does not record the failing subcheck. External-reference check 0 and zero exception fields elsewhere cannot prove that registry admission, memory-gate access, list capture or snapshot admission succeeded. No specific cause or restoration fix is claimed from this report.

Added checks, kept in the suite:
- Session: 1 inactive, 2 binding already owned, 3 serial exhausted, 4 runtime stopped, 5 external capture, 6 runtime image capture, 7 external revalidation, 8 collection; 0 success.
- Runtime image capture: 1 owner/activity, 2 memory gate/stopped, 3 environment before, 4 native list, 5 environment after, 6 environment mismatch, 7 registry construction, 8 snapshot admission; 0 success.
- Native list: 1 arguments, 2 head read, 3 capacity, 4 duplicate node, 5 vtable read, 6 supported type/extent, 7 payload copy, 8 type/previous/manager links, 9 head stability, 10 payload stability; 0 success.

The report includes object index/address/vtable, exception, stopped and failed-window state. No guard is relaxed. A failed environment check now explicitly leaves the output image invalid (the failed token already prevented its use). Regression coverage distinguishes unregistered objects, duplicate-frame admission, invalid memory and a later successful capture. A live failure is still needed to select a cause-specific fix.
