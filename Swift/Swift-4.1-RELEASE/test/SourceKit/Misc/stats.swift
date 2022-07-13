func foo() {}

// RUN: %sourcekitd-test -req=syntax-map %s == -req=stats | %FileCheck %s -check-prefix=SYNTAX_1

// SYNTAX_1: 2 {{.*}} source.statistic.num-requests
// SYNTAX_1: 0 {{.*}} source.statistic.num-semantic-requests
// SYNTAX_1: 0 {{.*}} source.statistic.num-ast-builds
// SYNTAX_1: 1 {{.*}} source.statistic.num-open-documents
// SYNTAX_1: 1 {{.*}} source.statistic.max-open-documents

// RUN: %sourcekitd-test -req=syntax-map %s == -req=close %s == -req=stats | %FileCheck %s -check-prefix=SYNTAX_2

// SYNTAX_2: 3 {{.*}} source.statistic.num-requests
// SYNTAX_2: 0 {{.*}} source.statistic.num-semantic-requests
// SYNTAX_2: 0 {{.*}} source.statistic.num-ast-builds
// SYNTAX_2: 0 {{.*}} source.statistic.num-open-documents
// SYNTAX_2: 1 {{.*}} source.statistic.max-open-documents

// RUN: %sourcekitd-test -req=sema %s -- %s == -req=stats | %FileCheck %s -check-prefix=SEMA_1

// SEMA_1: 3 {{.*}} source.statistic.num-requests
// SEMA_1: 0 {{.*}} source.statistic.num-semantic-requests
// SEMA_1: 1 {{.*}} source.statistic.num-ast-builds
// SEMA_1: 1 {{.*}} source.statistic.num-asts-in-memory
// SEMA_1: 1 {{.*}} source.statistic.max-asts-in-memory
// SEMA_1: 0 {{.*}} source.statistic.num-ast-cache-hits
// SEMA_1: 0 {{.*}} source.statistic.num-ast-snaphost-uses

// RUN: %sourcekitd-test -req=sema %s -- %s == -req=edit -pos=1:1 -replace=" " %s == -req=stats | %FileCheck %s -check-prefix=SEMA_2

// SEMA_2: 5 {{.*}} source.statistic.num-requests
// SEMA_2: 0 {{.*}} source.statistic.num-semantic-requests
// SEMA_2: 2 {{.*}} source.statistic.num-ast-builds
// SEMA_2: 1 {{.*}} source.statistic.num-asts-in-memory
// SEMA_2: 2 {{.*}} source.statistic.max-asts-in-memory
// SEMA_2: 0 {{.*}} source.statistic.num-ast-cache-hits
// SEMA_2: 0 {{.*}} source.statistic.num-ast-snaphost-uses

// RUN: %sourcekitd-test -req=sema %s -- %s == -req=cursor -pos=1:6 %s -- %s == -req=stats | %FileCheck %s -check-prefix=SEMA_3

// SEMA_3: 4 {{.*}} source.statistic.num-requests
// SEMA_3: 1 {{.*}} source.statistic.num-semantic-requests
// SEMA_3: 1 {{.*}} source.statistic.num-ast-builds
// SEMA_3: 1 {{.*}} source.statistic.num-asts-in-memory
// SEMA_3: 1 {{.*}} source.statistic.max-asts-in-memory
// SEMA_3: 0 {{.*}} source.statistic.num-ast-cache-hits
// SEMA_3: 1 {{.*}} source.statistic.num-ast-snaphost-uses

// RUN: %sourcekitd-test -req=sema %s -- %s == -req=related-idents -pos=1:6 %s -- %s == -req=stats | %FileCheck %s -check-prefix=SEMA_4

// SEMA_4: 4 {{.*}} source.statistic.num-requests
// SEMA_4: 1 {{.*}} source.statistic.num-semantic-requests
// SEMA_4: 1 {{.*}} source.statistic.num-ast-builds
// SEMA_4: 1 {{.*}} source.statistic.num-asts-in-memory
// SEMA_4: 1 {{.*}} source.statistic.max-asts-in-memory
// SEMA_4: 1 {{.*}} source.statistic.num-ast-cache-hits
// SEMA_4: 0 {{.*}} source.statistic.num-ast-snaphost-uses
