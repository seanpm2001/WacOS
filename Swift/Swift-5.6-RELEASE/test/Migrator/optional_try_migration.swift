// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -c -swift-version 4 -primary-file %s -emit-migrated-file-path %t/optional_try_migration.result.swift %api_diff_data_dir
// RUN: %diff -u %S/optional_try_migration.swift.expected %t/optional_try_migration.result.swift

func fetchOptInt() throws -> Int? {
    return 3
}

func fetchInt() throws -> Int {
    return 3
}

func fetchAny() throws -> Any {
    return 3
}

func testOnlyMigrateChangedBehavior() {
    // No migration needed
    let _ = try? fetchInt()

    // Migration needed
    let _ = try? fetchOptInt()
}

func testExplicitCasts() {

    // No migration needed, because there's an explicit cast on the try already
    let _ = (try? fetchOptInt()) as? Int

    // Migration needed; the 'as? Int' is part of the sub-expression
    let _ = try? fetchAny() as? Int

    // No migration needed; the subexpression is non-optional so behavior has not changed
    let _ = (try? fetchAny()) as? Int

    // No migration needed, because there's an explicit cast on the try already
    let _ = (try? fetchOptInt()) as! Int // expected-warning {{forced cast from 'Int??' to 'Int' only unwraps optionals; did you mean to use '!!'?}}

    // No migration needed; the subexpression is non-optional
    let _ = try? fetchAny() as! Int

    // No migration needed; the subexpression is non-optional so behavior has not changed
    let _ = (try? fetchAny()) as! Int

    // Migration needed; the explicit cast is not directly on the try?
    let _ = String(describing: try? fetchOptInt()) as Any

    // No migration needed, because the try's subexpression is non-optional
    let _ = String(describing: try? fetchInt()) as Any

}

func testOptionalChaining() {
    struct Thing {
        func fetchInt() throws -> Int { return 3 }
        func fetchOptInt() throws -> Int { return 3 }
    }

    let thing = Thing()
    let optThing: Thing? = Thing()

    // Migration needed
    let _ = try? optThing?.fetchInt()

    // Migration needed
    let _ = try? optThing?.fetchOptInt()

    // No migration needed
    let _ = try? optThing!.fetchOptInt()

    // No migration needed, because of the explicit cast
    let _ = (try? optThing?.fetchOptInt()) as? Int // expected-warning{{conditional downcast from 'Int?' to 'Int' does nothing}}

    // Migration needed
    let _ = try? thing.fetchInt()

    // Migration needed
    let _ = try? thing.fetchOptInt()

    // No migration needed, because of the explicit cast
    let _ = (try? thing.fetchOptInt()) as! Int // expected-warning {{forced cast from 'Int?' to 'Int' only unwraps optionals; did you mean to use '!'?}}
}


func testIfLet() {
    
    // Migration needed
    if let optionalX = try? fetchOptInt(),
        let x = optionalX
    {
        print(x)
    }
    
    // Don't change 'try?'s that haven't changed behavior
    if let x = try? fetchInt(),
        let y = try? fetchInt() {
        print(x, y)
    }
}


func testCaseMatching() {
    // Migration needed
    if case let x?? = try? fetchOptInt() {
        print(x)
    }

    // No migration needed
    if case let x? = try? fetchInt() {
        print(x)
    }
}
