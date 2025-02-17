/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Basic tests that verify our KURL's interface behaves the same as the
// original KURL's.

#include "platform/weborigin/KURL.h"

#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/WTFString.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/url_util.h"

namespace blink {

TEST(KURLTest, Getters) {
  struct GetterCase {
    const char* url;
    const char* protocol;
    const char* host;
    int port;
    const char* user;
    const char* pass;
    const char* path;
    const char* last_path_component;
    const char* query;
    const char* fragment_identifier;
    bool has_fragment_identifier;
  } cases[] = {
      {"http://www.google.com/foo/blah?bar=baz#ref", "http", "www.google.com",
       0, "", 0, "/foo/blah", "blah", "bar=baz", "ref", true},
      {// Non-ASCII code points in the fragment part. fragmentIdentifier()
       // shouldn't return it in percent-encoded form.
       "http://www.google.com/foo/blah?bar=baz#\xce\xb1\xce\xb2", "http",
       "www.google.com", 0, "", 0, "/foo/blah", "blah", "bar=baz",
       "\xce\xb1\xce\xb2", true},
      {"http://foo.com:1234/foo/bar/", "http", "foo.com", 1234, "", 0,
       "/foo/bar/", "bar", 0, 0, false},
      {"http://www.google.com?#", "http", "www.google.com", 0, "", 0, "/", 0,
       "", "", true},
      {"https://me:pass@google.com:23#foo", "https", "google.com", 23, "me",
       "pass", "/", 0, 0, "foo", true},
      {"javascript:hello!//world", "javascript", "", 0, "", 0, "hello!//world",
       "world", 0, 0, false},
      {// Recognize a query and a fragment in the path portion of a path
       // URL.
       "javascript:hello!?#/\\world", "javascript", "", 0, "", 0, "hello!",
       "hello!", "", "/\\world", true},
      {// lastPathComponent() method handles "parameters" in a path. path()
       // method doesn't.
       "http://a.com/hello;world", "http", "a.com", 0, "", 0, "/hello;world",
       "hello", 0, 0, false},
      {// IDNA
       "http://\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xbd\xa0\xe5\xa5\xbd/", "http",
       "xn--6qqa088eba", 0, "", 0, "/", 0, 0, 0, false},
  };

  for (size_t i = 0; i < WTF_ARRAY_LENGTH(cases); i++) {
    const GetterCase& c = cases[i];

    const String& url = String::FromUTF8(c.url);

    const KURL kurl(kParsedURLString, url);

    // Casted to the String (or coverted to using fromUTF8() for
    // expectations which may include non-ASCII code points) so that the
    // contents are printed on failure.
    EXPECT_EQ(String(c.protocol), kurl.Protocol()) << url;
    EXPECT_EQ(String(c.host), kurl.Host()) << url;
    EXPECT_EQ(c.port, kurl.Port()) << url;
    EXPECT_EQ(String(c.user), kurl.User()) << url;
    EXPECT_EQ(String(c.pass), kurl.Pass()) << url;
    EXPECT_EQ(String(c.path), kurl.GetPath()) << url;
    EXPECT_EQ(String(c.last_path_component), kurl.LastPathComponent()) << url;
    EXPECT_EQ(String(c.query), kurl.Query()) << url;
    if (c.has_fragment_identifier)
      EXPECT_EQ(String::FromUTF8(c.fragment_identifier),
                kurl.FragmentIdentifier())
          << url;
    else
      EXPECT_TRUE(kurl.FragmentIdentifier().IsNull()) << url;
  }
}

TEST(KURLTest, Setters) {
  // Replace the starting URL with the given components one at a time and
  // verify that we're always the same as the old KURL.
  //
  // Note that old KURL won't canonicalize the default port away, so we
  // can't set setting the http port to "80" (or even "0").
  //
  // We also can't test clearing the query.
  struct ExpectedComponentCase {
    const char* url;

    const char* protocol;
    const char* expected_protocol;

    const char* host;
    const char* expected_host;

    const int port;
    const char* expected_port;

    const char* user;
    const char* expected_user;

    const char* pass;
    const char* expected_pass;

    const char* path;
    const char* expected_path;

    const char* query;
    const char* expected_query;
  } cases[] = {
      {"http://www.google.com/",
       // protocol
       "https", "https://www.google.com/",
       // host
       "news.google.com", "https://news.google.com/",
       // port
       8888, "https://news.google.com:8888/",
       // user
       "me", "https://me@news.google.com:8888/",
       // pass
       "pass", "https://me:pass@news.google.com:8888/",
       // path
       "/foo", "https://me:pass@news.google.com:8888/foo",
       // query
       "?q=asdf", "https://me:pass@news.google.com:8888/foo?q=asdf"},
      {"https://me:pass@google.com:88/a?f#b",
       // protocol
       "http", "http://me:pass@google.com:88/a?f#b",
       // host
       "goo.com", "http://me:pass@goo.com:88/a?f#b",
       // port
       92, "http://me:pass@goo.com:92/a?f#b",
       // user
       "", "http://:pass@goo.com:92/a?f#b",
       // pass
       "", "http://goo.com:92/a?f#b",
       // path
       "/", "http://goo.com:92/?f#b",
       // query
       0, "http://goo.com:92/#b"},
  };

  for (size_t i = 0; i < WTF_ARRAY_LENGTH(cases); i++) {
    KURL kurl(kParsedURLString, cases[i].url);

    kurl.SetProtocol(cases[i].protocol);
    EXPECT_STREQ(cases[i].expected_protocol, kurl.GetString().Utf8().data());

    kurl.SetHost(cases[i].host);
    EXPECT_STREQ(cases[i].expected_host, kurl.GetString().Utf8().data());

    kurl.SetPort(cases[i].port);
    EXPECT_STREQ(cases[i].expected_port, kurl.GetString().Utf8().data());

    kurl.SetUser(cases[i].user);
    EXPECT_STREQ(cases[i].expected_user, kurl.GetString().Utf8().data());

    kurl.SetPass(cases[i].pass);
    EXPECT_STREQ(cases[i].expected_pass, kurl.GetString().Utf8().data());

    kurl.SetPath(cases[i].path);
    EXPECT_STREQ(cases[i].expected_path, kurl.GetString().Utf8().data());

    kurl.SetQuery(cases[i].query);
    EXPECT_STREQ(cases[i].expected_query, kurl.GetString().Utf8().data());

    // Refs are tested below. On the Safari 3.1 branch, we don't match their
    // KURL since we integrated a fix from their trunk.
  }
}

// Tests that KURL::decodeURLEscapeSequences works as expected
TEST(KURLTest, DecodeURLEscapeSequences) {
  struct DecodeCase {
    const char* input;
    const char* output;
  } decode_cases[] = {
      {"hello, world", "hello, world"},
      {"%01%02%03%04%05%06%07%08%09%0a%0B%0C%0D%0e%0f/",
       "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0B\x0C\x0D\x0e\x0f/"},
      {"%10%11%12%13%14%15%16%17%18%19%1a%1B%1C%1D%1e%1f/",
       "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1B\x1C\x1D\x1e\x1f/"},
      {"%20%21%22%23%24%25%26%27%28%29%2a%2B%2C%2D%2e%2f/",
       " !\"#$%&'()*+,-.//"},
      {"%30%31%32%33%34%35%36%37%38%39%3a%3B%3C%3D%3e%3f/",
       "0123456789:;<=>?/"},
      {"%40%41%42%43%44%45%46%47%48%49%4a%4B%4C%4D%4e%4f/",
       "@ABCDEFGHIJKLMNO/"},
      {"%50%51%52%53%54%55%56%57%58%59%5a%5B%5C%5D%5e%5f/",
       "PQRSTUVWXYZ[\\]^_/"},
      {"%60%61%62%63%64%65%66%67%68%69%6a%6B%6C%6D%6e%6f/",
       "`abcdefghijklmno/"},
      {"%70%71%72%73%74%75%76%77%78%79%7a%7B%7C%7D%7e%7f/",
       "pqrstuvwxyz{|}~\x7f/"},
      // Test un-UTF-8-ization.
      {"%e4%bd%a0%e5%a5%bd", "\xe4\xbd\xa0\xe5\xa5\xbd"},
  };

  for (size_t i = 0; i < WTF_ARRAY_LENGTH(decode_cases); i++) {
    String input(decode_cases[i].input);
    String str = DecodeURLEscapeSequences(input);
    EXPECT_STREQ(decode_cases[i].output, str.Utf8().data());
  }

  // Our decode should decode %00
  String zero = DecodeURLEscapeSequences("%00");
  EXPECT_STRNE("%00", zero.Utf8().data());

  // Decode UTF-8.
  String decoded = DecodeURLEscapeSequences("%e6%bc%a2%e5%ad%97");
  const UChar kDecodedExpected[] = {0x6F22, 0x5b57};
  EXPECT_EQ(String(kDecodedExpected, WTF_ARRAY_LENGTH(kDecodedExpected)),
            decoded);

  // Test the error behavior for invalid UTF-8 (we differ from WebKit here).
  String invalid = DecodeURLEscapeSequences("%e4%a0%e5%a5%bd");
  UChar invalid_expected_helper[4] = {0x00e4, 0x00a0, 0x597d, 0};
  String invalid_expected(
      reinterpret_cast<const ::UChar*>(invalid_expected_helper), 3);
  EXPECT_EQ(invalid_expected, invalid);
}

TEST(KURLTest, EncodeWithURLEscapeSequences) {
  struct EncodeCase {
    const char* input;
    const char* output;
  } encode_cases[] = {
      {"hello, world", "hello%2C%20world"},
      {"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F",
       "%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"},
      {"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F",
       "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"},
      {" !\"#$%&'()*+,-./", "%20!%22%23%24%25%26%27()*%2B%2C-./"},
      {"0123456789:;<=>?", "0123456789%3A%3B%3C%3D%3E%3F"},
      {"@ABCDEFGHIJKLMNO", "%40ABCDEFGHIJKLMNO"},
      {"PQRSTUVWXYZ[\\]^_", "PQRSTUVWXYZ%5B%5C%5D%5E_"},
      {"`abcdefghijklmno", "%60abcdefghijklmno"},
      {"pqrstuvwxyz{|}~\x7f", "pqrstuvwxyz%7B%7C%7D~%7F"},
  };

  for (size_t i = 0; i < WTF_ARRAY_LENGTH(encode_cases); i++) {
    String input(encode_cases[i].input);
    String expected_output(encode_cases[i].output);
    String output = EncodeWithURLEscapeSequences(input);
    EXPECT_EQ(expected_output, output);
  }

  // Our encode escapes NULLs for safety, so we need to check that too.
  String input("\x00\x01", 2);
  String reference("%00%01");

  String output = EncodeWithURLEscapeSequences(input);
  EXPECT_EQ(reference, output);

  // Also test that it gets converted to UTF-8 properly.
  UChar wide_input_helper[3] = {0x4f60, 0x597d, 0};
  String wide_input(reinterpret_cast<const ::UChar*>(wide_input_helper), 2);
  String wide_reference("%E4%BD%A0%E5%A5%BD");
  String wide_output = EncodeWithURLEscapeSequences(wide_input);
  EXPECT_EQ(wide_reference, wide_output);

  // Encoding should not NFC-normalize the string.
  // Contain a combining character ('e' + COMBINING OGONEK).
  String combining(String::FromUTF8("\x65\xCC\xA8"));
  EXPECT_EQ(EncodeWithURLEscapeSequences(combining), "e%CC%A8");
  // Contain a precomposed character corresponding to |combining|.
  String precomposed(String::FromUTF8("\xC4\x99"));
  EXPECT_EQ(EncodeWithURLEscapeSequences(precomposed), "%C4%99");
}

TEST(KURLTest, RemoveWhitespace) {
  struct {
    const char* input;
    const char* expected;
  } cases[] = {
      {"ht\ntps://example.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"ht\ttps://example.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"ht\rtps://example.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://exa\nmple.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://exa\tmple.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://exa\rmple.com/yay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/y\nay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/y\tay?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/y\ray?boo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?b\noo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?b\too#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?b\roo#foo", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?boo#f\noo", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?boo#f\too", "https://example.com/yay?boo#foo"},
      {"https://example.com/yay?boo#f\roo", "https://example.com/yay?boo#foo"},
  };

  for (const auto& test : cases) {
    const KURL input(kParsedURLString, test.input);
    const KURL expected(kParsedURLString, test.expected);
    EXPECT_EQ(input, expected);
    EXPECT_TRUE(input.WhitespaceRemoved());
    EXPECT_FALSE(expected.WhitespaceRemoved());
  }
}

TEST(KURLTest, ResolveEmpty) {
  KURL empty_base;

  // WebKit likes to be able to resolve absolute input agains empty base URLs,
  // which would normally be invalid since the base URL is invalid.
  const char kAbs[] = "http://www.google.com/";
  KURL resolve_abs(empty_base, kAbs);
  EXPECT_TRUE(resolve_abs.IsValid());
  EXPECT_STREQ(kAbs, resolve_abs.GetString().Utf8().data());

  // Resolving a non-relative URL agains the empty one should still error.
  const char kRel[] = "foo.html";
  KURL resolve_err(empty_base, kRel);
  EXPECT_FALSE(resolve_err.IsValid());
}

// WebKit will make empty URLs and set components on them. kurl doesn't allow
// replacements on invalid URLs, but here we do.
TEST(KURLTest, ReplaceInvalid) {
  KURL kurl;

  EXPECT_FALSE(kurl.IsValid());
  EXPECT_TRUE(kurl.IsEmpty());
  EXPECT_STREQ("", kurl.GetString().Utf8().data());

  kurl.SetProtocol("http");
  // GKURL will say that a URL with just a scheme is invalid, KURL will not.
  EXPECT_FALSE(kurl.IsValid());
  EXPECT_FALSE(kurl.IsEmpty());
  // At this point, we do things slightly differently if there is only a scheme.
  // We check the results here to make it more obvious what is going on, but it
  // shouldn't be a big deal if these change.
  EXPECT_STREQ("http:", kurl.GetString().Utf8().data());

  kurl.SetHost("www.google.com");
  EXPECT_TRUE(kurl.IsValid());
  EXPECT_FALSE(kurl.IsEmpty());
  EXPECT_STREQ("http://www.google.com/", kurl.GetString().Utf8().data());

  kurl.SetPort(8000);
  EXPECT_TRUE(kurl.IsValid());
  EXPECT_FALSE(kurl.IsEmpty());
  EXPECT_STREQ("http://www.google.com:8000/", kurl.GetString().Utf8().data());

  kurl.SetPath("/favicon.ico");
  EXPECT_TRUE(kurl.IsValid());
  EXPECT_FALSE(kurl.IsEmpty());
  EXPECT_STREQ("http://www.google.com:8000/favicon.ico",
               kurl.GetString().Utf8().data());

  // Now let's test that giving an invalid replacement fails. Invalid
  // protocols fail without modifying the URL, which should remain valid.
  EXPECT_FALSE(kurl.SetProtocol("f/sj#@"));
  EXPECT_TRUE(kurl.IsValid());
}

TEST(KURLTest, Valid_HTTP_FTP_URLsHaveHosts) {
  // Since the suborigin schemes are added at the content layer, its
  // necessary it explicitly add them as standard schemes for this test. If
  // this is needed in the future across mulitple KURLTests, then KURLTest
  // should probably be converted to a test fixture with a proper SetUp()
  // method.
  url::AddStandardScheme("http-so", url::SCHEME_WITH_PORT);
  url::AddStandardScheme("https-so", url::SCHEME_WITH_PORT);

  KURL kurl(kParsedURLString, "foo://www.google.com/");
  EXPECT_TRUE(kurl.SetProtocol("http"));
  EXPECT_TRUE(kurl.ProtocolIs("http"));
  EXPECT_TRUE(kurl.ProtocolIsInHTTPFamily());
  EXPECT_TRUE(kurl.IsValid());

  EXPECT_TRUE(kurl.SetProtocol("http-so"));
  EXPECT_TRUE(kurl.ProtocolIs("http-so"));
  EXPECT_TRUE(kurl.IsValid());

  EXPECT_TRUE(kurl.SetProtocol("https"));
  EXPECT_TRUE(kurl.ProtocolIs("https"));
  EXPECT_TRUE(kurl.IsValid());

  EXPECT_TRUE(kurl.SetProtocol("https-so"));
  EXPECT_TRUE(kurl.ProtocolIs("https-so"));
  EXPECT_TRUE(kurl.IsValid());

  EXPECT_TRUE(kurl.SetProtocol("ftp"));
  EXPECT_TRUE(kurl.ProtocolIs("ftp"));
  EXPECT_TRUE(kurl.IsValid());

  kurl = KURL(KURL(), "http://");
  EXPECT_FALSE(kurl.ProtocolIs("http"));

  kurl = KURL(KURL(), "http://wide#鸡");
  EXPECT_TRUE(kurl.ProtocolIs("http"));
  EXPECT_EQ(kurl.Protocol(), "http");

  kurl = KURL(KURL(), "http-so://foo");
  EXPECT_TRUE(kurl.ProtocolIs("http-so"));

  kurl = KURL(KURL(), "https://foo");
  EXPECT_TRUE(kurl.ProtocolIs("https"));

  kurl = KURL(KURL(), "https-so://foo");
  EXPECT_TRUE(kurl.ProtocolIs("https-so"));

  kurl = KURL(KURL(), "ftp://foo");
  EXPECT_TRUE(kurl.ProtocolIs("ftp"));

  kurl = KURL(KURL(), "http://host/");
  EXPECT_TRUE(kurl.IsValid());
  kurl.SetHost("");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "http-so://host/");
  EXPECT_TRUE(kurl.IsValid());
  kurl.SetHost("");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "https://host/");
  EXPECT_TRUE(kurl.IsValid());
  kurl.SetHost("");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "https-so://host/");
  EXPECT_TRUE(kurl.IsValid());
  kurl.SetHost("");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "ftp://host/");
  EXPECT_TRUE(kurl.IsValid());
  kurl.SetHost("");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "http:///noodles/pho.php");
  EXPECT_STREQ("http://noodles/pho.php", kurl.GetString().Utf8().data());
  EXPECT_STREQ("noodles", kurl.Host().Utf8().data());
  EXPECT_TRUE(kurl.IsValid());

  kurl = KURL(KURL(), "https://username:password@/");
  EXPECT_FALSE(kurl.IsValid());

  kurl = KURL(KURL(), "https://username:password@host/");
  EXPECT_TRUE(kurl.IsValid());
}

TEST(KURLTest, Path) {
  const char kInitial[] = "http://www.google.com/path/foo";
  KURL kurl(kParsedURLString, kInitial);

  // Clear by setting a null string.
  String null_string;
  EXPECT_TRUE(null_string.IsNull());
  kurl.SetPath(null_string);
  EXPECT_STREQ("http://www.google.com/", kurl.GetString().Utf8().data());
}

// Test that setting the query to different things works. Thq query is handled
// a littler differently than some of the other components.
TEST(KURLTest, Query) {
  const char kInitial[] = "http://www.google.com/search?q=awesome";
  KURL kurl(kParsedURLString, kInitial);

  // Clear by setting a null string.
  String null_string;
  EXPECT_TRUE(null_string.IsNull());
  kurl.SetQuery(null_string);
  EXPECT_STREQ("http://www.google.com/search", kurl.GetString().Utf8().data());

  // Clear by setting an empty string.
  kurl = KURL(kParsedURLString, kInitial);
  String empty_string("");
  EXPECT_FALSE(empty_string.IsNull());
  kurl.SetQuery(empty_string);
  EXPECT_STREQ("http://www.google.com/search?", kurl.GetString().Utf8().data());

  // Set with something that begins in a question mark.
  const char kQuestion[] = "?foo=bar";
  kurl.SetQuery(kQuestion);
  EXPECT_STREQ("http://www.google.com/search?foo=bar",
               kurl.GetString().Utf8().data());

  // Set with something that doesn't begin in a question mark.
  const char kQuery[] = "foo=bar";
  kurl.SetQuery(kQuery);
  EXPECT_STREQ("http://www.google.com/search?foo=bar",
               kurl.GetString().Utf8().data());
}

TEST(KURLTest, Ref) {
  KURL kurl(kParsedURLString, "http://foo/bar#baz");

  // Basic ref setting.
  KURL cur(kParsedURLString, "http://foo/bar");
  cur.SetFragmentIdentifier("asdf");
  EXPECT_STREQ("http://foo/bar#asdf", cur.GetString().Utf8().data());
  cur = kurl;
  cur.SetFragmentIdentifier("asdf");
  EXPECT_STREQ("http://foo/bar#asdf", cur.GetString().Utf8().data());

  // Setting a ref to the empty string will set it to "#".
  cur = KURL(kParsedURLString, "http://foo/bar");
  cur.SetFragmentIdentifier("");
  EXPECT_STREQ("http://foo/bar#", cur.GetString().Utf8().data());
  cur = kurl;
  cur.SetFragmentIdentifier("");
  EXPECT_STREQ("http://foo/bar#", cur.GetString().Utf8().data());

  // Setting the ref to the null string will clear it altogether.
  cur = KURL(kParsedURLString, "http://foo/bar");
  cur.SetFragmentIdentifier(String());
  EXPECT_STREQ("http://foo/bar", cur.GetString().Utf8().data());
  cur = kurl;
  cur.SetFragmentIdentifier(String());
  EXPECT_STREQ("http://foo/bar", cur.GetString().Utf8().data());
}

TEST(KURLTest, Empty) {
  KURL kurl;

  // First test that regular empty URLs are the same.
  EXPECT_TRUE(kurl.IsEmpty());
  EXPECT_FALSE(kurl.IsValid());
  EXPECT_TRUE(kurl.IsNull());
  EXPECT_TRUE(kurl.GetString().IsNull());
  EXPECT_TRUE(kurl.GetString().IsEmpty());

  // Test resolving a null URL on an empty string.
  KURL kurl2(kurl, "");
  EXPECT_FALSE(kurl2.IsNull());
  EXPECT_TRUE(kurl2.IsEmpty());
  EXPECT_FALSE(kurl2.IsValid());
  EXPECT_FALSE(kurl2.GetString().IsNull());
  EXPECT_TRUE(kurl2.GetString().IsEmpty());
  EXPECT_FALSE(kurl2.GetString().IsNull());
  EXPECT_TRUE(kurl2.GetString().IsEmpty());

  // Resolve the null URL on a null string.
  KURL kurl22(kurl, String());
  EXPECT_FALSE(kurl22.IsNull());
  EXPECT_TRUE(kurl22.IsEmpty());
  EXPECT_FALSE(kurl22.IsValid());
  EXPECT_FALSE(kurl22.GetString().IsNull());
  EXPECT_TRUE(kurl22.GetString().IsEmpty());
  EXPECT_FALSE(kurl22.GetString().IsNull());
  EXPECT_TRUE(kurl22.GetString().IsEmpty());

  // Test non-hierarchical schemes resolving. The actual URLs will be different.
  // WebKit's one will set the string to "something.gif" and we'll set it to an
  // empty string. I think either is OK, so we just check our behavior.
  KURL kurl3(KURL(kParsedURLString, "data:foo"), "something.gif");
  EXPECT_TRUE(kurl3.IsEmpty());
  EXPECT_FALSE(kurl3.IsValid());

  // Test for weird isNull string input,
  // see: http://bugs.webkit.org/show_bug.cgi?id=16487
  KURL kurl4(kParsedURLString, kurl.GetString());
  EXPECT_TRUE(kurl4.IsEmpty());
  EXPECT_FALSE(kurl4.IsValid());
  EXPECT_TRUE(kurl4.GetString().IsNull());
  EXPECT_TRUE(kurl4.GetString().IsEmpty());

  // Resolving an empty URL on an invalid string.
  KURL kurl5(KURL(), "foo.js");
  // We'll be empty in this case, but KURL won't be. Should be OK.
  // EXPECT_EQ(kurl5.isEmpty(), kurl5.isEmpty());
  // EXPECT_EQ(kurl5.getString().isEmpty(), kurl5.getString().isEmpty());
  EXPECT_FALSE(kurl5.IsValid());
  EXPECT_FALSE(kurl5.GetString().IsNull());

  // Empty string as input
  KURL kurl6(kParsedURLString, "");
  EXPECT_TRUE(kurl6.IsEmpty());
  EXPECT_FALSE(kurl6.IsValid());
  EXPECT_FALSE(kurl6.GetString().IsNull());
  EXPECT_TRUE(kurl6.GetString().IsEmpty());

  // Non-empty but invalid C string as input.
  KURL kurl7(kParsedURLString, "foo.js");
  // WebKit will actually say this URL has the string "foo.js" but is invalid.
  // We don't do that.
  // EXPECT_EQ(kurl7.isEmpty(), kurl7.isEmpty());
  EXPECT_FALSE(kurl7.IsValid());
  EXPECT_FALSE(kurl7.GetString().IsNull());
}

TEST(KURLTest, UserPass) {
  const char* src = "http://user:pass@google.com/";
  KURL kurl(kParsedURLString, src);

  // Clear just the username.
  kurl.SetUser("");
  EXPECT_EQ("http://:pass@google.com/", kurl.GetString());

  // Clear just the password.
  kurl = KURL(kParsedURLString, src);
  kurl.SetPass("");
  EXPECT_EQ("http://user@google.com/", kurl.GetString());

  // Now clear both.
  kurl.SetUser("");
  EXPECT_EQ("http://google.com/", kurl.GetString());
}

TEST(KURLTest, Offsets) {
  const char* src1 = "http://user:pass@google.com/foo/bar.html?baz=query#ref";
  KURL kurl1(kParsedURLString, src1);

  EXPECT_EQ(17u, kurl1.HostStart());
  EXPECT_EQ(27u, kurl1.HostEnd());
  EXPECT_EQ(27u, kurl1.PathStart());
  EXPECT_EQ(40u, kurl1.PathEnd());
  EXPECT_EQ(32u, kurl1.PathAfterLastSlash());

  const char* src2 = "http://google.com/foo/";
  KURL kurl2(kParsedURLString, src2);

  EXPECT_EQ(7u, kurl2.HostStart());
  EXPECT_EQ(17u, kurl2.HostEnd());
  EXPECT_EQ(17u, kurl2.PathStart());
  EXPECT_EQ(22u, kurl2.PathEnd());
  EXPECT_EQ(22u, kurl2.PathAfterLastSlash());

  const char* src3 = "javascript:foobar";
  KURL kurl3(kParsedURLString, src3);

  EXPECT_EQ(11u, kurl3.HostStart());
  EXPECT_EQ(11u, kurl3.HostEnd());
  EXPECT_EQ(11u, kurl3.PathStart());
  EXPECT_EQ(17u, kurl3.PathEnd());
  EXPECT_EQ(11u, kurl3.PathAfterLastSlash());
}

TEST(KURLTest, DeepCopy) {
  const char kUrl[] = "http://www.google.com/";
  KURL src(kParsedURLString, kUrl);
  EXPECT_TRUE(src.GetString() ==
              kUrl);  // This really just initializes the cache.
  KURL dest = src.Copy();
  EXPECT_TRUE(dest.GetString() ==
              kUrl);  // This really just initializes the cache.

  // The pointers should be different for both UTF-8 and UTF-16.
  EXPECT_NE(dest.GetString().Impl(), src.GetString().Impl());
}

TEST(KURLTest, DeepCopyInnerURL) {
  const char kUrl[] = "filesystem:http://www.google.com/temporary/test.txt";
  const char kInnerURL[] = "http://www.google.com/temporary";
  KURL src(kParsedURLString, kUrl);
  EXPECT_TRUE(src.GetString() == kUrl);
  EXPECT_TRUE(src.InnerURL()->GetString() == kInnerURL);
  KURL dest = src.Copy();
  EXPECT_TRUE(dest.GetString() == kUrl);
  EXPECT_TRUE(dest.InnerURL()->GetString() == kInnerURL);
}

TEST(KURLTest, LastPathComponent) {
  KURL url1(kParsedURLString, "http://host/path/to/file.txt");
  EXPECT_EQ("file.txt", url1.LastPathComponent());

  KURL invalid_utf8(kParsedURLString, "http://a@9%aa%:/path/to/file.txt");
  EXPECT_EQ(String(), invalid_utf8.LastPathComponent());
}

TEST(KURLTest, IsHierarchical) {
  KURL url1(kParsedURLString, "http://host/path/to/file.txt");
  EXPECT_TRUE(url1.IsHierarchical());

  KURL invalid_utf8(kParsedURLString, "http://a@9%aa%:/path/to/file.txt");
  EXPECT_FALSE(invalid_utf8.IsHierarchical());
}

TEST(KURLTest, PathAfterLastSlash) {
  KURL url1(kParsedURLString, "http://host/path/to/file.txt");
  EXPECT_EQ(20u, url1.PathAfterLastSlash());

  KURL invalid_utf8(kParsedURLString, "http://a@9%aa%:/path/to/file.txt");
  EXPECT_EQ(0u, invalid_utf8.PathAfterLastSlash());
}

TEST(KURLTest, ProtocolIsInHTTPFamily) {
  KURL url1(kParsedURLString, "http://host/path/to/file.txt");
  EXPECT_TRUE(url1.ProtocolIsInHTTPFamily());

  KURL invalid_utf8(kParsedURLString, "http://a@9%aa%:/path/to/file.txt");
  EXPECT_FALSE(invalid_utf8.ProtocolIsInHTTPFamily());
}

TEST(KURLTest, ProtocolIs) {
  KURL url1(kParsedURLString, "foo://bar");
  EXPECT_TRUE(url1.ProtocolIs("foo"));
  EXPECT_FALSE(url1.ProtocolIs("foo-bar"));

  KURL url2(kParsedURLString, "foo-bar:");
  EXPECT_TRUE(url2.ProtocolIs("foo-bar"));
  EXPECT_FALSE(url2.ProtocolIs("foo"));

  KURL invalid_utf8(kParsedURLString, "http://a@9%aa%:");
  EXPECT_FALSE(invalid_utf8.ProtocolIs("http"));

  KURL capital(KURL(), "HTTP://www.example.text");
  EXPECT_TRUE(capital.ProtocolIs("http"));
  EXPECT_EQ(capital.Protocol(), "http");
}

TEST(KURLTest, strippedForUseAsReferrer) {
  struct ReferrerCase {
    const char* input;
    const char* output;
  } referrer_cases[] = {
      {"data:text/html;charset=utf-8,<html></html>", ""},
      {"javascript:void(0);", ""},
      {"about:config", ""},
      {"https://www.google.com/", "https://www.google.com/"},
      {"http://me@news.google.com:8888/", "http://news.google.com:8888/"},
      {"http://:pass@news.google.com:8888/foo",
       "http://news.google.com:8888/foo"},
      {"http://me:pass@news.google.com:8888/", "http://news.google.com:8888/"},
      {"https://www.google.com/a?f#b", "https://www.google.com/a?f"},
      {"file:///tmp/test.html", ""},
      {"https://www.google.com/#", "https://www.google.com/"},
  };

  for (size_t i = 0; i < WTF_ARRAY_LENGTH(referrer_cases); i++) {
    KURL kurl(kParsedURLString, referrer_cases[i].input);
    String referrer = kurl.StrippedForUseAsReferrer();
    EXPECT_STREQ(referrer_cases[i].output, referrer.Utf8().data());
  }
}

}  // namespace blink
