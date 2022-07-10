// RUN: %target-swift-ide-test -print-comments -source-filename %S/Inputs/comment_extensions.swift -comments-xml-schema %S/../../bindings/xml/comment-xml-schema.rng | %FileCheck %s

// Content is in separate file in ./Inputs due to the "requires" keyword getting
// recognized by lit.

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}} line="{{.*}}" column="{{.*}}"><Name>attention()</Name><USR>s:F14swift_ide_test9attentionFT_T_</USR><Declaration>func attention()</Declaration><Discussion><Attention><Para>This function is so hip and exciting, it can’t be trusted.</Para></Attention></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>author()</Name><USR>s:F14swift_ide_test6authorFT_T_</USR><Declaration>func author()</Declaration><Discussion><Author><Para>Stephen</Para></Author></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>authors()</Name><USR>s:F14swift_ide_test7authorsFT_T_</USR><Declaration>func authors()</Declaration><Discussion><Authors><Para></Para><List-Bullet><Item><Para>Homer</Para></Item><Item><Para>Mark</Para></Item><Item><Para>J.</Para></Item></List-Bullet></Authors></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>bug()</Name><USR>s:F14swift_ide_test3bugFT_T_</USR><Declaration>func bug()</Declaration><Discussion><Bug><Para>rdar://problem/8675309</Para></Bug></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>complexity()</Name><USR>s:F14swift_ide_test10complexityFT_T_</USR><Declaration>func complexity()</Declaration><Discussion><Complexity><Para>O(n log2(n))</Para></Complexity></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>copyright()</Name><USR>s:F14swift_ide_test9copyrightFT_T_</USR><Declaration>func copyright()</Declaration><Discussion><Copyright><Para>2015 Apple, Inc.</Para></Copyright></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>date()</Name><USR>s:F14swift_ide_test4dateFT_T_</USR><Declaration>func date()</Declaration><Discussion><Date><Para>Thu Apr 23 22:38:09 PDT 2015</Para></Date></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>experiment()</Name><USR>s:F14swift_ide_test10experimentFT_T_</USR><Declaration>func experiment()</Declaration><Discussion><Experiment><Para>Try some more. The strawberries taste like strawberries.</Para></Experiment></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Class file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>Invariant</Name><USR>s:V14swift_ide_test9Invariant</USR><Declaration>struct Invariant</Declaration><Discussion><Invariant><Para>x not nil</Para></Invariant></Discussion></Class>]

// CHECK: {{.*}}DocCommentAsXML=none

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>note()</Name><USR>s:F14swift_ide_test4noteFT_T_</USR><Declaration>func note()</Declaration><Discussion><Note><Para>This function is very hip and exciting.</Para></Note></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>postcondition(_:)</Name><USR>s:F14swift_ide_test13postconditionFRSiT_</USR><Declaration>func postcondition(_ x: inout Int)</Declaration><Discussion><Postcondition><Para>x is unchanged</Para></Postcondition></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=none

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>precondition(_:)</Name><USR>s:F14swift_ide_test12preconditionFSiT_</USR><Declaration>func precondition(_ x: Int)</Declaration><Discussion><Precondition><Para><codeVoice>x &lt; 100</codeVoice></Para></Precondition></Discussion></Function>]
// CHECK: {{.*}}DocCommentAsXML=none

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>remark()</Name><USR>s:F14swift_ide_test6remarkFT_T_</USR><Declaration>func remark()</Declaration><Discussion><Remark><Para>Always, no, never forget to check your references.</Para></Remark></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>remarks()</Name><USR>s:F14swift_ide_test7remarksFT_T_</USR><Declaration>func remarks()</Declaration><Discussion><Remarks><Para></Para><List-Bullet><Item><Para>Never let a bear approach you.</Para></Item></List-Bullet></Remarks></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>requires()</Name><USR>s:F14swift_ide_test8requiresFT_T_</USR><Declaration>func requires()</Declaration><Discussion><Requires><Para></Para><List-Bullet><Item><Para>explicit package name. Just kidding!</Para></Item></List-Bullet></Requires></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>see()</Name><USR>s:F14swift_ide_test3seeFT_T_</USR><Declaration>func see()</Declaration><Discussion><See><Para>the pie (it’s very good).</Para></See></Discussion></Function>] CommentXMLValid

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>since()</Name><USR>s:F14swift_ide_test5sinceFT_T_</USR><Declaration>func since()</Declaration><Discussion><Since><Para>1809</Para></Since></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>todo()</Name><USR>s:F14swift_ide_test4todoFT_T_</USR><Declaration>func todo()</Declaration><Discussion><TODO><Para>be</Para></TODO><TODO><Para>or not to be</Para></TODO></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>version()</Name><USR>s:F14swift_ide_test7versionFT_T_</USR><Declaration>func version()</Declaration><Discussion><Version><Para>Beta.</Para></Version></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>warning()</Name><USR>s:F14swift_ide_test7warningFT_T_</USR><Declaration>func warning()</Declaration><Discussion><Warning><Para>Share the road.</Para></Warning></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>imageWithEmptyURLAndBogusTitle()</Name><USR>s:F14swift_ide_test30imageWithEmptyURLAndBogusTitleFT_T_</USR><Declaration>func imageWithEmptyURLAndBogusTitle()</Declaration><Abstract><Para><rawHTML><![CDATA[<img src="" alt="/bogus/url/as/title"\>]]></rawHTML></Para></Abstract></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>imageTitleAndAlt()</Name><USR>s:F14swift_ide_test16imageTitleAndAltFT_T_</USR><Declaration>func imageTitleAndAlt()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para><rawHTML><![CDATA[<img src="/swift.png" title="Image Title" alt="Image Alt"\>]]></rawHTML></Para></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>imageAlt()</Name><USR>s:F14swift_ide_test8imageAltFT_T_</USR><Declaration>func imageAlt()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para><rawHTML><![CDATA[<img src="/swift.png" alt="Image Alt"\>]]></rawHTML></Para></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>imageTitle()</Name><USR>s:F14swift_ide_test10imageTitleFT_T_</USR><Declaration>func imageTitle()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para><rawHTML><![CDATA[<img src="/swift.png" title="Image Title" alt="Image Alt"\>]]></rawHTML></Para></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>urlWithQueryString()</Name><USR>s:F14swift_ide_test18urlWithQueryStringFT_T_</USR><Declaration>func urlWithQueryString()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para>Test <Link href="http://apple.com?a=1&amp;b=1&amp;c=abc">a link</Link></Para></Discussion></Function>]

// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>imageWithAmpersandsInTitleAndAlt()</Name><USR>s:F14swift_ide_test32imageWithAmpersandsInTitleAndAltFT_T_</USR><Declaration>func imageWithAmpersandsInTitleAndAlt()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para><rawHTML><![CDATA[<img src="http://apple.com" title="&&&" alt="&&&"\>]]></rawHTML></Para></Discussion></Function>]
