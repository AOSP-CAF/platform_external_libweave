# Copyright 2015 The Weave Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'target_defaults': {
    'configurations': {
      'Release': {
        'defines': [
          'NDEBUG',
        ],
        'cflags': [
          '-Os',
        ],
      },
      'Debug': {
        'defines': [
          '_DEBUG',
        ],
        'cflags': [
          '-O0  ',
          '-g3',
        ],
      },
    },
    'include_dirs': [
      '.',
      'include',
      'third_party/chromium',
      'third_party/include',
      'third_party/libuweave',
      'third_party/modp_b64/modp_b64',
    ],
    'cflags!': ['-fPIE'],
    'cflags': [
      '-fno-exceptions',
      '-fPIC',
      '-fvisibility=hidden',
      '-Wall',
      '-Werror',
      '-Wextra',
      '-Wl,--exclude-libs,ALL',
      '-Wno-char-subscripts',
      '-Wno-format-nonliteral',
      '-Wno-missing-field-initializers',
      '-Wno-unused-local-typedefs',
      '-Wno-unused-parameter',
      '-Wpacked',
      '-Wpointer-arith',
      '-Wwrite-strings',
    ],
    'cflags_cc': [
      '-std=c++11',
    ],
    'cflags_c': [
      '-std=c99',
    ],
    'libraries': [
      # 'library_dirs' does not work as expected with make files
      '-Lthird_party/lib',
    ],
    'library_dirs': ['third_party/lib']
  },
}