from __future__ import print_function
import sys  # noqa: I201
from kinds import SYNTAX_BASE_KINDS, kind_to_type, lowercase_first_word


def error(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(-1)


class Node(object):
    """
    A Syntax node, possibly with children.
    If the kind is "SyntaxCollection", then this node is considered a Syntax
    Collection that will expose itself as a typedef rather than a concrete
    subclass.
    """

    def __init__(self, name, kind=None, children=None,
                 element=None, element_name=None):
        self.syntax_kind = name
        self.swift_syntax_kind = lowercase_first_word(name)
        self.name = kind_to_type(self.syntax_kind)

        self.children = children or []
        self.base_kind = kind
        self.base_type = kind_to_type(self.base_kind)

        if self.base_kind not in SYNTAX_BASE_KINDS:
            error("unknown base kind '%s' for node '%s'" %
                  (self.base_kind, self.syntax_kind))

        self.collection_element = element or ""
        # If there's a preferred name for the collection element that differs
        # from its supertype, use that.
        self.collection_element_name = element_name or self.collection_element
        self.collection_element_type = kind_to_type(self.collection_element)

    def is_base(self):
        """
        Returns `True` if this node declares one of the base syntax kinds.
        """
        return self.syntax_kind in SYNTAX_BASE_KINDS

    def is_syntax_collection(self):
        """
        Returns `True` if this node is a subclass of SyntaxCollection.
        """
        return self.base_kind == "SyntaxCollection"

    def requires_validation(self):
        """
        Returns `True` if this node should have a `valitate` method associated.
        """
        return self.is_buildable()

    def is_unknown(self):
        """
        Returns `True` if this node is an `Unknown` syntax subclass.
        """
        return "Unknown" in self.syntax_kind

    def is_buildable(self):
        """
        Returns `True` if this node should have a builder associated.
        """
        return not self.is_base() and \
            not self.is_unknown() and \
            not self.is_syntax_collection()
