#ifndef TEST_INTEROP_CXX_VALUE_WITNESS_TABLE_INPUTS_COPY_CONSTRUCTORS_H
#define TEST_INTEROP_CXX_VALUE_WITNESS_TABLE_INPUTS_COPY_CONSTRUCTORS_H

struct HasUserProvidedCopyConstructor {
  int numCopies;
  HasUserProvidedCopyConstructor(int numCopies = 0) : numCopies(numCopies) {}
  HasUserProvidedCopyConstructor(const HasUserProvidedCopyConstructor &other)
      : numCopies(other.numCopies + 1) {}
};

struct HasNonTrivialImplicitCopyConstructor {
  HasUserProvidedCopyConstructor box;
  HasNonTrivialImplicitCopyConstructor()
      : box(HasUserProvidedCopyConstructor()) {}
};

struct HasNonTrivialDefaultCopyConstructor {
  HasUserProvidedCopyConstructor box;
  HasNonTrivialDefaultCopyConstructor()
      : box(HasUserProvidedCopyConstructor()) {}
  HasNonTrivialDefaultCopyConstructor(
      const HasNonTrivialDefaultCopyConstructor &) = default;
};

// Make sure that we don't crash on struct templates with copy-constructors.
template <typename T> struct S {
  S(S const &) {}
};

#endif // TEST_INTEROP_CXX_VALUE_WITNESS_TABLE_INPUTS_COPY_CONSTRUCTORS_H
