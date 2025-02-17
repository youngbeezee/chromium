// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const incrementer_url = '../wasm/incrementer.wasm';

function TestStreamedCompile() {
  return fetch(incrementer_url)
    .then(WebAssembly.compile)
    .then(m => new WebAssembly.Instance(m))
    .then(i => assert_equals(5, i.exports.increment(4)));
}

function TestShortFormStreamedCompile() {
  return WebAssembly.compile(fetch(incrementer_url))
    .then(m => new WebAssembly.Instance(m))
    .then(i => assert_equals(5, i.exports.increment(4)));
}

function NegativeTestStreamedCompilePromise() {
  return WebAssembly.compile(new Promise((resolve, reject)=>{resolve(5);}))
    .then(assert_unreached,
          e => assert_true(e instanceof TypeError));
}

function CompileBlankResponse() {
  return WebAssembly.compile(new Response())
    .then(assert_unreached,
          e => assert_true(e instanceof TypeError));
}

function InstantiateBlankResponse() {
  return WebAssembly.instantiate(new Response())
    .then(assert_unreached,
          e => assert_true(e instanceof TypeError));
}

function CompileFromArrayBuffer() {
  return fetch(incrementer_url)
    .then(r => r.arrayBuffer())
    .then(arr => new Response(arr))
    .then(WebAssembly.compile)
    .then(m => new WebAssembly.Instance(m))
    .then(i => assert_equals(6, i.exports.increment(5)));
}

function CompileFromInvalidArrayBuffer() {
  var arr = new ArrayBuffer(10);
  var view = new Uint8Array(arr);
  for (var i = 0; i < view.length; ++i) view[i] = i;

  return WebAssembly.compile(new Response(arr))
    .then(assert_unreached,
          e => assert_true(e instanceof Error));
}

function InstantiateFromInvalidArrayBuffer() {
  var arr = new ArrayBuffer(10);
  var view = new Uint8Array(arr);
  for (var i = 0; i < view.length; ++i) view[i] = i;

  return WebAssembly.instantiate(new Response(arr))
    .then(assert_unreached,
          e => assert_true(e instanceof Error));
}

function TestStreamedInstantiate() {
  return fetch(incrementer_url)
    .then(WebAssembly.instantiate)
    .then(pair => assert_equals(5, pair.instance.exports.increment(4)));
}

function InstantiateFromArrayBuffer() {
  return fetch(incrementer_url)
    .then(response => response.arrayBuffer())
    .then(WebAssembly.instantiate)
    .then(pair => assert_equals(5, pair.instance.exports.increment(4)));
}

function TestShortFormStreamedInstantiate() {
  return WebAssembly.instantiate(fetch(incrementer_url))
    .then(pair => assert_equals(5, pair.instance.exports.increment(4)));
}

function InstantiateFromInvalidArrayBuffer() {
  var arr = new ArrayBuffer(10);
  var view = new Uint8Array(arr);
  for (var i = 0; i < view.length; ++i) view[i] = i;

  return WebAssembly.compile(new Response(arr))
    .then(assert_unreached,
          e => assert_true(e instanceof Error));
}

function buildImportingModuleBytes() {
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "memory", 1);
  var kSig_v_i = makeSig([kWasmI32], []);
  var signature = builder.addType(kSig_v_i);
  builder.addImport("", "some_value", kSig_i_v);
  builder.addImport("", "writer", signature);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprGetLocal, 0,
      kExprI32LoadMem, 0, 0,
      kExprI32Const, 1,
      kExprCallIndirect, signature, kTableZero,
      kExprGetLocal,0,
      kExprI32LoadMem,0, 0,
      kExprCallFunction, 0,
      kExprI32Add
    ]).exportFunc();

  // writer(mem[i]);
  // return mem[i] + some_value();
  builder.addFunction("_wrap_writer", signature)
    .addBody([
      kExprGetLocal, 0,
      kExprCallFunction, 1]);
  builder.appendToTable([2, 3]);

  var wire_bytes = builder.toBuffer();
  return wire_bytes;
}

function TestInstantiateComplexModule() {
  var mem_1 = new WebAssembly.Memory({initial: 1});
  var view_1 = new Int32Array(mem_1.buffer);
  view_1[0] = 42;
  var outval_1;

  var ffi = {"":
             {some_value: () => 1,
              writer: (x) => outval_1 = x ,
              memory: mem_1}
            };
  return Promise.resolve(buildImportingModuleBytes())
    .then(b => WebAssembly.instantiate(b, ffi))
    .then(pair => assert_true(pair.instance instanceof WebAssembly.Instance));
}
