/*
 Copyright (c) 2012, 2014, Oracle and/or its affiliates. All rights
 reserved.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2 of
 the License.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 02110-1301  USA
 */

/* SQL Tokenizer
   tokenize(source, operators)
   IMMEDIATE
     
   source:    a source string
   operators: a string consisting of all legal one-character operators

   Returns an array of tokens.  
   Token types:
     name = alphabetic { alpha + numeric + underscore } (SQL REGULAR IDENTIFIER)
     number = digit { digit } .                     (NON-NEGATIVE INTEGERS ONLY)
     variable = '@' { char } .                              (MYSQL "@" VARIABLE)

     operator = valid single-character operator
     string  = text quoted in single, double, or backtick quotes
   
   Ignores C-style block comments and SQL-style comments from "--" TO EOL

   Stops scanning and returns token stream if it reaches the special token
   BEGINDATA
*/

function tokenize (source) {
  var operators = "(),;:.";   // Legal one-character operators
  var result = [];            // An array to hold the results

  var c;                      // The current character
  var i;                      // The index of the current character
  var v;                      // Intermediate value
  var tok;                    // Current token
  var q;                      // Quote character
  var line = 1, col = 1;      // Current line and column of input

  function peek() {           // Look ahead one character 
    return source.charAt(i+1);
  }
  
  function advance(n) {       // Advance to next character
    var amt = n || 1;
    if(i + amt >= source.length) {
      i = source.length;
      c = '';
    }
    else { 
      i += amt;
      c = source.charAt(i);
    }
    if(c == '\n') {  line += 1;  col = 0; }
    else          {  col += amt; }
  }

  function begin() {          // Begin tokenizing
    i = 0;
    c = source.charAt(i);
    if(c == '\n') { line = 1; }
  }

  function isAlpha() { 
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
  }
  
  function isAlpha_() {
    return (c === '_'  || isAlpha());
  }

  function isNumeric() {
    return (c >= '0' && c <= '9');
  }

  function isInitialNumeric() {
    var p = peek();
    return (isNumeric() || (c == '-' && p >= '0' && p <= '9'))
  }

  function isNonInitialNumeric() {
    return (isNumeric() || (c == '.'));
  }
  
  function isAlphanumeric() {
    return (isAlpha_() || isNumeric());
  }

  /* Tokens */
  function Token(type, initialValue) {
    this.type = type;
    this.line = line;
    this.column = col;
    this.str = initialValue;
    advance();
  }

  Token.prototype.consume = function() {
    this.str += c;
    advance();
  };
  
  Token.prototype.deliver = function(value) {
    this.value = value || this.str;
    if(debug) console.log("Token deliver", this.type, this.value);
    delete this.str;
    result.push(this);
  };
  
  Token.prototype.error = function(message) {
    var err = new Error(message);
    err.token = this;
    throw err;
  };

  /* Examine the text one character at a time. */
  begin();
  while (c) {
    tok = null;
  
    if (c <= ' ') {                                      /* IGNORE WHITESPACE */
      advance(); 
    }

    else if(isAlpha()) {                                              /* NAME */
      tok = new Token('name', c);
      while (isAlphanumeric()) {
        tok.consume(c);
      }
      tok.deliver();
    }

    else if (c === '@') {                                        /* @VARIABLE */
      tok = new Token('variable', '');
      while (isAlphanumeric()) {
        tok.consume();
      }
      tok.deliver();
    }
    
    else if (isInitialNumeric()) {                                  /* NUMBER */
      tok = new Token('number', c);
      while(isNonInitialNumeric()) {
        tok.consume();
      }
      v = + tok.str;  // numeric value
      if(isFinite(v))  { tok.deliver(v); }
      else             { tok.error("bad number") };
    }
        
    else if (c === '\'' || c === '"' || c === '`') {         /* QUOTED STRING */
      q = c;
      tok = new Token('string', '');
      while (c !== q) {                       /* until closing quote */

        /* Special cases: unterminated string, control character, escapes */
        if (c === '\n' || c === '\r' || c === '') {
          tok.error("Unterminated string.");
        }
        else if (c < ' ') { 
          tok.error("Control character in string.");
        }
        else if (c === '\\') {  /* escape sequence */
          advance();
          switch (c) {
            case '':
              tok.error("Unterminated string");
              break;
            case 'b':
              c = '\b'; break;
            case 'f':
              c = '\f'; break;
            case 'n':
              c = '\n'; break;
            case 'r':
              c = '\r'; break;
            case 't':
              c = '\t'; break;
            case 'u':
              v = parseInt(source.substr(i + 1, 4), 16);
              if (v < 0 || v > 0xFFFF) {
                tok.error("Bad Unicode character sequence");
              }
              c = String.fromCharCode(v);
              advance(4);
              break;
          }
        }

        tok.consume(); 
      }

      advance(); /* advance past closing quote */
      tok.deliver();
    }
    
    else if (c === '-' && peek() === '-') {        /* COMMENTS FROM -- TO EOL */
      advance(2);
      while(c !== '\n' && c !== '\r' && c !== '') {
        advance();
      }
    }

    else if (c === '/' && peek() === '*') {            // COMMENTS FROM /* TO */
      advance(2);
      while(c && c !== '*' && peek() !== '/') {
        advance();
      }
      advance(2);
      if(c === '') { throw Error("Unterminated comment"); }
    }
  
    else if(operators.indexOf(c) >= 0) {         /* SINGLE-CHARACTER OPERATOR */
      tok = new Token('operator', c);
      tok.deliver();
    }
    
    else {
      v = "scanner error";
      if(result.length) {  v += " after " + result.pop().value;  }
      v += " at position " + i;
      throw new Error(v);
    }

  }  /* end of while loop */

  return result;
};



////////////////////////////////////////////////////////////////
//
// Data File Scanners
//

function Scanner(source, start, options) {
  this.source = source;
  this.i = start;
  this.c = source.charAt(start);
  this.opt = options;
  this.EOL = options.lineEndString;
  this.lineEndExtra = (this.EOL.length > 1);
}

Scanner.prototype.advance = function(n) {
  assert(n > 0);
  this.i += n;
  if(this.i < this.source.length) {
    this.c = this.source.charAt(this.i);
  } else {
    this.i = this.source.length - 1;
    this.c = '';
  }
};

Scanner.prototype.peek = function(chars) {
  var n = chars || 1;
  return this.source[this.i + n];
};

Scanner.prototype.isQuote = function(character) {
  var char = character || this.c;
  return ((char === this.opt.fieldQuoteStart) ||
          (char === this.opt.fieldQuoteEnd));
};

Scanner.prototype.isEsc = function() {
  return (this.c === this.opt.fieldQuoteEsc);
};

Scanner.prototype.isWhitespace = function() {
  return (this.c <= ' ');      /// FIXME <= ' ' is too broad
};

Scanner.prototype.skipWhitespace = function() {
  if(! this.opt.semanticWhitespace) {
    while(this.c && this.isWhitespace()) {
      this.advance(1);
    }
  }
};

Scanner.prototype.isStartQuote = function() {
  return (this.c === this.opt.fieldQuoteStart);
};

Scanner.prototype.isFieldSeparator = function() {
  return ( (this.c === this.opt.fieldSep) ||
           (this.fieldSepOnWhitespace && this.isWhitespace()));
};

Scanner.prototype.handleQuotedString = function(doEval) {
  var inquote, value, consume, scanner;
  if(doEval)  {
    scanner = this;
    value = "";
    consume = function() { value += scanner.c };
  } else {
    consume = function() {};
  }

  assert(this.c === this.opt.fieldQuoteStart);

  inquote = true;
  do {
    this.advance(1);

    if(this.isQuote() && (this.c === this.peek())) {      /**** Doubled quote */
      consume();       /* CONSUME A QUOTE */
      this.advance(2); /* SKIP PAST A QUOTE */
    }
    else if(this.c === this.opt.fieldQuoteEnd) {          /**** Closing quote */
      this.advance(1); /* ADVANCE PAST CLOSING QUOTE CHAR */
      inquote = false; /* TERMINATE LOOP */
    }
    else if(this.isEsc()) {                               /** Escape Sequence */
      this.advance(1); /* SKIP PAST ESCAPE CHAR */
      if(this.isQuote()) {                     // quote
        consume();
        this.advance(1);
      }
    }
    else {                                                /* Normal character */
      consume();
    }
  } while(inquote && this.c);

  return value;
};

Scanner.prototype.skip_C_comment = function() {
  if (this.c === '/' && this.peek() === '*') {
    this.advance(2);
    while(this.c && this.c !== '*' && this.peek() !== '/') {
      this.advance(1);
    }
    this.advance(2);
    if(this.c === '') {
      throw Error("Unterminated comment");
    }
  }
};

Scanner.prototype.isEndOfLine = function() {
  var r;
  if(this.lineEndExtra) {
    r = (this.c === this.EOL);
  } else {
    r = (this.source.substr(this.i, this.EOL.length) === this.EOL);
  }
  return r;
};

Scanner.prototype.skipToEndOfLine = function() {
  while(this.c) {
    if(this.isEndOfLine()) {
      this.advance(this.EOL.length);
      return;
    } else {
      this.advance(1);
    }
  }
};

Scanner.prototype.isInlineComment = function() {
  var i, j;

  if(! this.opt.commentStart) {
    return false;
  }

  j = this.opt.commentStart.length;
  for(i = 0 ; i < j ; i++) {
    if(this.peek(i) !== this.opt.commentStart.charAt(i)) {
      return false;
    }
  }
  return true;
};

Scanner.prototype.skipLinePrefix = function() {
  var idx;
  if(this.opt.lineStartString && this.opt.lineStartString.length) {
    idx = this.source.indexOf(this.opt.lineStartString, this.i);
    if(idx > 0) {
      this.i = idx + this.opt.lineStartString.length;
      this.c = this.source.charAt(this.i);
      this.skipWhitespace();
    }
  }
};

Scanner.prototype.isAtEnd = function() {
  return ((1 + this.i) >= this.source.length);
};

Scanner.prototype.getValueForDelimitedField = function() {
  var value;
  if(this.isStartQuote()) {
    value = this.handleQuotedString(true);
  } else {
    value = "";
    while(! ( this.isFieldSeparator() || this.isEndOfLine())) {
      /* TODO: Handle \N for null; also handle other escape sequences? */
      value += this.c;
      this.advance(1);
    }
  }
  return value;
};

Scanner.prototype.getValueForFixedWidthField = function(column) {
  return this.source.substring(column.startPos, column.endPos);
};


/* The LineScanner scans a string read from an input buffer.
   It is responsible for identifying the start and end of the 
   next logical line containing data.

   LineScannerSpec maintains state information for the LineScanner
   with regards to a particular string source (i.e. one buffer of
   data read from a file).
*/
function LineScannerSpec(source) {
  this.source   = source;
  this.start    = 0;
  this.end      = 0;
  this.complete = false;  // string from start to end contains a complete record
  this.eof      = false;  // end of buffer reached
};

function LineScanner(options) {
  this.options = options;
  this.options.semanticWhitespace =
    (options.fieldSepOnWhitespace || (options.fieldSep < ' '));
};

LineScanner.prototype.newSpec = function(source) {
  return new LineScannerSpec(source);
}

/* scan():
   start in string spec.source at position spec.start.
   Skip over whitespace and comments.
   Set spec.start to the beginning of the next record containing data.
   If the buffer contains the end of the record, set spec.end;
   otherwise, set spec.eof.
*/
LineScanner.prototype.scan = function(spec) {
  var scanner = new Scanner(spec.source, spec.start, this.options);
  var start, isEOL;

  /* Find start of line */
  do {
    start = scanner.i;
    scanner.skipWhitespace();
    if(scanner.isInlineComment()) {
     scanner.skipToEndOfLine();
    }
  } while(scanner.i > start);  // while making progress through the file

  scanner.skipLinePrefix();  // brings us to the end of prefix (if any)
  spec.start = scanner.i;

  /* Find end of line; set EOF if not reached */
  do {
    scanner.advance(1);
    if(scanner.isStartQuote()) {
      scanner.handleQuotedString(false);
    }
    isEOL = scanner.isEndOfLine();
  } while(scanner.c && ! isEOL);

  spec.eof = scanner.isAtEnd();
  spec.complete = isEOL;
  spec.end = scanner.i;
}


/* The FieldScanner works on a line that has already been delimited by the
   LineScanner.  It splits the line into fields and retuns an object 
   containing those fields.
*/

function TextFieldScanner(columns, options) {
  this.columns = columns;
  this.options = options;
};



/* testFileType investigates a JSON file.
   We allow C and C++ style comments.
   If it starts with "[ [" or "[ {"  it's a JSON array.
   If it starts with "{" it's line-delimited JSON.
   RETURNS: 2 for JSON Array.  1 for line-delimited JSON.  0 for Not JSON.
*/
function testFileType(source) {
  var i, c, x;
  i = 0;
  var tokens = [];

  do {
    x = i;
    i = skipWhitespace(source, i);
    i = skipJavascriptComment(source, i); //fixme
    c = source[i];
    if(c === '[' || c === '{') {
      tokens.push(c); i++
    }
  } while(i > x && tokens.length < 2)

  if(tokens[0] === "[" && tokens.length === 2) return 2;
  else if(tokens.length > 0) return 1;
  else return 0;
}


exports.tokenize = tokenize;
exports.LineScannerSpec = LineScannerSpec;
exports.LineScanner = LineScanner;
exports.TextFieldScanner = TextFieldScanner;
