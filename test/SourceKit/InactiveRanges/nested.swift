#if os(macOS)
class Foo {
    #if MY_FLAG
    func debugOnly() {
        
    }
    #endif
}
#elseif os(iOS)
class GracefulFallback {
    
}
#else
class Fallback {
    
}
#endif

// RUN: %sourcekitd-test -req=inactive-ranges %s -- -module-name inactive_ranges %s | %FileCheck -check-prefix=CHECK1 %s
// CHECK1: START IF CONFIGS
// CHECK1-NEXT: 1:1 - active
// CHECK1-NEXT: 3:5 - inactive
// CHECK1-NEXT: 9:1 - inactive
// CHECK1-NEXT: 13:1 - inactive
// CHECK1-NEXT: END IF CONFIGS
