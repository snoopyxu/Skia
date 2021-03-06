
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#import <Cocoa/Cocoa.h>
#include "SkApplication.h"

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    application_init();
    int retVal =  NSApplicationMain(argc, (const char **)argv);
    
#if 0
    // we don't expect NSApplicationMain to return. See our applicationShouldTerminate handler.
    application_term();
    [pool release];
#endif
    return retVal;
}
