Scope tokens in syntax files
============================

The rules of the syntax files installed with coole name, where a color
stood, what the text is:

    keyword  whole  if      KEYWORD_CONTROL
    context  " " STRING_QUOTED_DOUBLE
        keyword  \\"     CONSTANT_CHARACTER_ESCAPE

How the text looks is the skin's: the same file is drawn in the colors of
every skin, and the same kind of text looks the same in every language.
Rules that name colors, yellow or rgb345, keep working as they always did,
in the syntax files of the user above all.  The syntax highlighting section
of coole(1) describes the rules themselves.


How tokens work
---------------

A token is a word of capitals, digits and underscores with at least one
capital.  It is read as a scope, the token in lower case with dots for its
underscores:

    KEYWORD_CONTROL_IMPORT   ->   keyword.control.import

which is a kind of keyword.control, which is a kind of keyword.  The scopes
are the ones Sublime Text and TextMate use,
https://www.sublimetext.com/docs/scope_naming.html.

A keyword with a token of its own, in a context colored by a token, also
knows the scope of that context: an escape in a string is
"string.quoted.double constant.character.escape" as well.

A background or attributes written after the token stand over the classic
colors of the token, but not over a background or attributes a skin gives
the token itself, so that a rule keeps its classic look and a skin can still
restyle it:

    keyword  linestart  \t  default  red         (a red background alone)
    keyword  whole  linestart  #!*\n  COMMENT_LINE  black

A token may stand in a define, and on the number, string and symbols lines
of line-local rules.


The colors of the tokens
------------------------

The section [syntax] of the skin gives them.  A key is a scope, or the scope
of a context and a scope with a blank between them; the value is
foreground;background;attributes, as everywhere in a skin:

    [syntax]
        comment = gray
        keyword = yellow
        keyword.control = brightmagenta
        string = green
        string constant.character.escape = white;red
        invalid = white;red;bold

A token is looked up as Sublime Text matches its selectors: the longest part
of its own scope the skin names wins, keyword.control.import before
keyword.control before keyword, and at each length the scope together with
the scope of its context before the scope alone.  What the skin does not
name at all keeps the colors mcedit gave that kind of text, the column
"classic" below.  The skins gruvbox256, nord256, catppuccin256,
catppuccin-latte256, kanagawa256, rose-pine256, everforest256 and
tokyo-night-16M name the kinds of text in the colors of their themes.

Options -> Appearance changes the skin, and the highlighting with it, at
once.


Token reference
---------------

These are the tokens the installed syntax files use, by the root of their
scope.  "classic" is the color a skin that names nothing gives them;
"default" is the color of the text of the editor.

Comments
    COMMENT_LINE                   brown      // ..., # ..., -- ...
    COMMENT_BLOCK                  brown      /* ... */
    COMMENT_BLOCK_DOCUMENTATION    brown      /** ... */, (** ... *)

Keywords
    KEYWORD_CONTROL                yellow     if, for, while, return, case
    KEYWORD_CONTROL_IMPORT         yellow     import, include, use, using
    KEYWORD_OPERATOR               yellow     + - * / = == < > && ||
    KEYWORD_OPERATOR_WORD          yellow     and, or, not, instanceof, sizeof
    KEYWORD_OTHER                  yellow     other keywords of the language
    KEYWORD_OTHER_RESERVED         yellow     reserved or less common keywords
    KEYWORD_OTHER_NEW              yellow     new, delete
    KEYWORD_OTHER_PACKAGE          yellow     package, namespace
    KEYWORD_CONTROL_CONDITIONAL    yellow     conditions of a user menu entry
    KEYWORD_OPERATOR_LOGICAL       yellow     ! & | of those conditions
    KEYWORD_OTHER_TODO             brightred  BUG, FIXME, TODO, NOTE, !!!, ???
                                              inside comments

Storage (declarations and types)
    STORAGE_TYPE                   yellow     declaring words: let, var, typedef
    STORAGE_TYPE_PRIMITIVE         yellow     int, char, float, bool, void
    STORAGE_TYPE_CLASS             yellow     class, struct, enum, interface
    STORAGE_TYPE_FUNCTION          yellow     function, def, func, fun, fn, sub
    STORAGE_TYPE_ANNOTATION        yellow     @Override, @Component, decorators
    STORAGE_MODIFIER               yellow     public, static, const, final

Constants
    CONSTANT_LANGUAGE              yellow     true, false, null, nil, None
    CONSTANT_NUMERIC               brightgreen  42, 0x1F, 3.14e-2
    CONSTANT_CHARACTER             brightgreen  'a'
    CONSTANT_CHARACTER_ESCAPE      brightgreen  \n, \t, \\, &amp;
    CONSTANT_OTHER_PLACEHOLDER     brightgreen  %d, %s in format strings
    CONSTANT_OTHER                 brightgreen  other named constants
    CONSTANT_OTHER_TRAILER         brightgreen  Signed-off-by: in git messages

Strings
    STRING_QUOTED_DOUBLE           green      "..."
    STRING_QUOTED_SINGLE           green      '...'
    STRING_QUOTED_OTHER            green      `...`, heredocs, <file.h>
    STRING_UNQUOTED                green      unquoted values, block scalars
    STRING_REGEXP                  green      regular expressions
    STRING_OTHER_MATH              green      LaTeX $...$ and equations

Support (things provided by the language or its libraries)
    SUPPORT_FUNCTION               brightred  built-in and library functions,
                                              shell commands
    SUPPORT_CLASS                  brightred  built-in classes and modules
    SUPPORT_TYPE                   brightred  built-in types
    SUPPORT_CONSTANT               brightred  built-in names and settings
    SUPPORT_OTHER                  brightred  other provided names
    SUPPORT_TYPE_PROPERTY_NAME     yellow     keys of JSON objects

Names being defined
    ENTITY_NAME_FUNCTION           brightcyan  function name in a definition
    ENTITY_NAME_CLASS              brightcyan  class name in a definition
    ENTITY_NAME_TAG                brightcyan  HTML/XML tags, mapping keys
    ENTITY_NAME_SECTION            brightcyan  section headers ([section], %build)
    ENTITY_NAME_LABEL              brightcyan  goto labels, label@
    ENTITY_NAME_REFERENCE          brightcyan  #123 in git messages
    ENTITY_NAME_OTHER              brightcyan  other defined names
    ENTITY_OTHER_ATTRIBUTE_NAME    default    attributes, field names
    ENTITY_OTHER_PSEUDO_CLASS      default    CSS :hover, :first-child
    ENTITY_OTHER_PSEUDO_ELEMENT    default    CSS ::before, ::selection

Variables
    VARIABLE_LANGUAGE              yellow     this, self, super
    VARIABLE_OTHER                 default    $x, ${x}, %VAR%
    VARIABLE_OTHER_MEMBER          default    .length, .sizeof
    VARIABLE_PARAMETER             default    command-line flags, parameters
    VARIABLE_FUNCTION              default    the name of a function called

Punctuation
    PUNCTUATION_SECTION            brightcyan  { } ( ) [ ]
    PUNCTUATION_SEPARATOR          brightcyan  , : . ?
    PUNCTUATION_SEPARATOR_CONTINUATION  brightcyan  line continuation \
                                              (yellow inside META_PREPROCESSOR)
    PUNCTUATION_TERMINATOR         brightmagenta  ;
    PUNCTUATION                    brightcyan  the symbols of line-local rules

Meta (regions of a file)
    META_PREPROCESSOR              brightred  #include, #define lines, pragmas,
                                              template tags {% %}
    META_EMBEDDED                  default    code in another language: <? ?>
    META_HEADER                    default    mail header blocks
    META_DIFF_HEADER               brightmagenta  diff file headers ---, +++
    META_DIFF_RANGE                brightcyan  diff hunk headers @@ ... @@

Markup (prose and documents)
    MARKUP_HEADING                 brightcyan  headings
    MARKUP_BOLD                    white      bold text
    MARKUP_ITALIC                  yellow     italic text
    MARKUP_UNDERLINE_LINK          yellow     links, URLs, e-mail addresses
    MARKUP_LIST                    yellow     list bullets
    MARKUP_QUOTE                   green      quoted text: "> " in mail
    MARKUP_RAW                     green      verbatim text
    MARKUP_RAW_BLOCK               green      fenced and indented code blocks
    MARKUP_RAW_INLINE              green      `inline code`
    MARKUP_INSERTED                brightgreen  diff: added lines
    MARKUP_DELETED                 brightred  diff: removed lines
    MARKUP_CHANGED                 yellow     diff: changed lines

Invalid
    INVALID_ILLEGAL                white on red  syntax errors
    INVALID_DEPRECATED             white on red  deprecated commands and functions

Inside META_PREPROCESSOR, strings are red and punctuation is yellow.

Other tokens can be used too.  A skin that names neither the token nor a
shorter scope of it, and no classic color, leaves the text in the color of
the editor.


Recommendations for syntax file authors
* Checking a syntax file
----------------------

check-tokens.py, kept with the syntax files in the source tree
(misc/syntax/), finds every foreground that is a color instead of a token:

    python3 misc/syntax/check-tokens.py               all *.syntax files
    python3 misc/syntax/check-tokens.py my.syntax     one file
    python3 misc/syntax/check-tokens.py --strict      the "# TODO scope" ones too

It ends with status 1 when it finds one.  meson test runs it with --strict
as check_tokens.

The idea of the tokens, the converted syntax files and check-tokens.py
come from mc pull request #5189 by Henady Zakalusky (hza), which took the
scope names over from Sublime Text; in coole the colors are the skin's.
