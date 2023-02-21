#if os(macOS) > 10
class Foo {
}
#else
class Fallback {
}
#endif
// RUN: %sourcekitd-test -req=inactive-ranges %s -- -module-name inactive_ranges %s | %FileCheck -check-prefix=CHECK1 %s
// CHECK1: START IF CONFIGS
// CHECK1-NEXT: 1:1 - inactive
// CHECK1-NEXT: 4:1 - active
// CHECK1-NEXT: END IF CONFIGS
