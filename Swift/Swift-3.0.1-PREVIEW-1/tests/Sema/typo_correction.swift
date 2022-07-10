// RUN: %target-parse-verify-swift

// This is close enough to get typo-correction.
func test_short_and_close() {
  let foo = 4 // expected-note {{did you mean 'foo'?}}
  let bab = fob + 1 // expected-error {{use of unresolved identifier}}
}

// This is not.
func test_too_different() {
  let moo = 4
  let bbb = mbb + 1 // expected-error {{use of unresolved identifier}}
}

struct Whatever {}
func *(x: Whatever, y: Whatever) {}

// This works even for single-character identifiers.
func test_very_short() {
  // Note that we don't suggest operators.
  let x = 0 // expected-note {{did you mean 'x'?}}
  let longer = y // expected-error {{use of unresolved identifier 'y'}}
}

// It does not trigger in a variable's own initializer.
func test_own_initializer() {
  let x = y // expected-error {{use of unresolved identifier 'y'}}
}

// Report candidates that are the same distance in different ways.
func test_close_matches() {
  let match1 = 0 // expected-note {{did you mean 'match1'?}}
  let match22 = 0 // expected-note {{did you mean 'match22'?}}
  let x = match2 // expected-error {{use of unresolved identifier 'match2'}}
}

// Report not-as-good matches if they're still close enough to the best.
func test_keep_if_not_too_much_worse() {
  let longmatch1 = 0 // expected-note {{did you mean 'longmatch1'?}}
  let longmatch22 = 0 // expected-note {{did you mean 'longmatch22'?}}
  let x = longmatch // expected-error {{use of unresolved identifier 'longmatch'}}
}

// Report not-as-good matches if they're still close enough to the best.
func test_drop_if_too_different() {
  let longlongmatch1 = 0 // expected-note {{did you mean 'longlongmatch1'?}}
  let longlongmatch2222 = 0
  let x = longlongmatch // expected-error {{use of unresolved identifier 'longlongmatch'}}
}

// Candidates are suppressed if we have too many that are the same distance.
func test_too_many_same() {
  let match1 = 0
  let match2 = 0
  let match3 = 0
  let match4 = 0
  let match5 = 0
  let match6 = 0
  let x = match // expected-error {{use of unresolved identifier 'match'}}
}

// But if some are better than others, just drop the worse tier.
func test_too_many_but_some_better() {
  let mtch1 = 0 // expected-note {{did you mean 'mtch1'?}}
  let mtch2 = 0 // expected-note {{did you mean 'mtch2'?}}
  let match3 = 0
  let match4 = 0
  let match5 = 0
  let match6 = 0
  let x = mtch // expected-error {{use of unresolved identifier 'mtch'}}
}
