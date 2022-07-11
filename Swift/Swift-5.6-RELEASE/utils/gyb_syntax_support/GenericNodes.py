from .Child import Child
from .Node import Node  # noqa: I201

GENERIC_NODES = [
    # generic-where-clause -> 'where' requirement-list
    Node('GenericWhereClause', kind='Syntax',
         children=[
             Child('WhereKeyword', kind='WhereToken'),
             Child('RequirementList', kind='GenericRequirementList',
                   collection_element_name='Requirement'),
         ]),

    Node('GenericRequirementList', kind='SyntaxCollection',
         element='GenericRequirement',
         element_name='GenericRequirement'),

    # generic-requirement ->
    #     (same-type-requrement|conformance-requirement) ','?
    Node('GenericRequirement', kind='Syntax',
         traits=['WithTrailingComma'],
         children=[
             Child('Body', kind='Syntax',
                   node_choices=[
                       Child('SameTypeRequirement',
                             kind='SameTypeRequirement'),
                       Child('ConformanceRequirement',
                             kind='ConformanceRequirement'),
                   ]),
             Child('TrailingComma', kind='CommaToken',
                   is_optional=True),
         ]),

    # same-type-requirement -> type-identifier == type
    Node('SameTypeRequirement', kind='Syntax',
         children=[
             Child('LeftTypeIdentifier', kind='Type'),
             Child('EqualityToken', kind='Token',
                   token_choices=[
                       'SpacedBinaryOperatorToken',
                       'UnspacedBinaryOperatorToken',
                       'PrefixOperatorToken',
                       'PostfixOperatorToken',
                   ]),
             Child('RightTypeIdentifier', kind='Type'),
         ]),

    Node('GenericParameterList', kind='SyntaxCollection',
         element='GenericParameter'),

    # generic-parameter -> type-name
    #                    | type-name : type-identifier
    #                    | type-name : protocol-composition-type
    Node('GenericParameter', kind='Syntax',
         traits=['WithTrailingComma'],
         children=[
             Child('Attributes', kind='AttributeList',
                   collection_element_name='Attribute', is_optional=True),
             Child('Name', kind='IdentifierToken'),
             Child('Colon', kind='ColonToken',
                   is_optional=True),
             Child('InheritedType', kind='Type',
                   is_optional=True),
             Child('TrailingComma', kind='CommaToken',
                   is_optional=True),
         ]),

    # generic-parameter-clause -> '<' generic-parameter-list '>'
    Node('GenericParameterClause', kind='Syntax',
         children=[
             Child('LeftAngleBracket', kind='LeftAngleToken'),
             Child('GenericParameterList', kind='GenericParameterList',
                   collection_element_name='GenericParameter'),
             Child('RightAngleBracket', kind='RightAngleToken'),
         ]),

    # conformance-requirement -> type-identifier : type-identifier
    Node('ConformanceRequirement', kind='Syntax',
         children=[
             Child('LeftTypeIdentifier', kind='Type'),
             Child('Colon', kind='ColonToken'),
             Child('RightTypeIdentifier', kind='Type'),
         ]),
]
