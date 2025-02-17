# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Methods for converting model objects to human-readable formats."""

import datetime
import itertools
import time

import models


def _PrettySize(size):
  # Arbitrarily chosen cut-off.
  if abs(size) < 2000:
    return '%d bytes' % size
  # Always show 3 digits.
  size /= 1024.0
  if abs(size) < 10:
    return '%.2fkb' % size
  elif abs(size) < 100:
    return '%.1fkb' % size
  elif abs(size) < 1024:
    return '%dkb' % size
  size /= 1024.0
  if abs(size) < 10:
    return '%.2fmb' % size
  # We shouldn't be seeing sizes > 100mb.
  return '%.1fmb' % size


def _DiffPrefix(diff, sym):
  if diff.IsAdded(sym):
    return '+ '
  if diff.IsRemoved(sym):
    return '- '
  if sym.size:
    return '~ '
  return '= '


class Describer(object):
  def __init__(self, verbose=False, recursive=False):
    self.verbose = verbose
    self.recursive = recursive

  def _DescribeSectionSizes(self, section_sizes):
    relevant_names = models.SECTION_TO_SECTION_NAME.values()
    section_names = sorted(k for k in section_sizes.iterkeys()
                           if k in relevant_names or k.startswith('.data'))
    total_bytes = sum(v for k, v in section_sizes.iteritems()
                      if k in section_names and k != '.bss')
    yield ''
    yield 'Section Sizes (Total={:,} bytes):'.format(total_bytes)
    for name in section_names:
      size = section_sizes[name]
      if name == '.bss':
        yield '    {}: {:,} bytes (not included in totals)'.format(name, size)
      else:
        percent = float(size) / total_bytes if total_bytes else 0
        yield '    {}: {:,} bytes ({:.1%})'.format(name, size, percent)

    if self.verbose:
      yield ''
      yield 'Other section sizes:'
      section_names = sorted(k for k in section_sizes.iterkeys()
                             if k not in section_names)
      for name in section_names:
        yield '    {}: {:,} bytes'.format(name, section_sizes[name])

  def _DescribeSymbol(self, sym):
    if sym.IsGroup():
      address = 'Group'
    else:
      address = hex(sym.address)
    if self.verbose:
      count_part = '  count=%d' % len(sym) if sym.IsGroup() else ''
      yield '{}@{:<9s}  size={}  padding={}  size_without_padding={}{}'.format(
          sym.section, address, sym.size, sym.padding, sym.size_without_padding,
          count_part)
      yield '    source_path={} \tobject_path={}'.format(
          sym.source_path, sym.object_path)
      if sym.name:
        yield '    flags={}  name={}'.format(sym.FlagsString(), sym.name)
        if sym.full_name:
          yield '               full_name={}'.format(sym.full_name)
      elif sym.full_name:
        yield '    flags={}  full_name={}'.format(
            sym.FlagsString(), sym.full_name)
    else:
      yield '{}@{:<9s}  {:<7} {}'.format(
          sym.section, address, sym.size,
          sym.source_path or sym.object_path or '{no path}')
      if sym.name:
        count_part = ' (count=%d)' % len(sym) if sym.IsGroup() else ''
        yield '    {}{}'.format(sym.name, count_part)

  def _DescribeSymbolGroupChildren(self, group, indent=0):
    running_total = 0
    sorted_syms = group if group.is_sorted else group.Sorted()
    is_diff = isinstance(group, models.SymbolDiff)

    indent_prefix = '> ' * indent
    diff_prefix = ''
    for s in sorted_syms:
      if group.IsBss() or not s.IsBss():
        running_total += s.size
      for l in self._DescribeSymbol(s):
        if l[:4].isspace():
          indent_size = 8 + len(indent_prefix) + len(diff_prefix)
          yield '{} {}'.format(' ' * indent_size, l)
        else:
          if is_diff:
            diff_prefix = _DiffPrefix(group, s)
          yield '{}{}{:8} {}'.format(indent_prefix, diff_prefix,
                                     running_total, l)

      if self.recursive and s.IsGroup():
        for l in self._DescribeSymbolGroupChildren(s, indent=indent + 1):
          yield l

  def _DescribeSymbolGroup(self, group):
    total_size = group.size
    code_syms = group.WhereInSection('t')
    code_size = code_syms.size
    ro_size = code_syms.Inverted().WhereInSection('r').size
    unique_paths = set(s.object_path for s in group)
    header_desc = [
        'Showing {:,} symbols with total size: {} bytes'.format(
            len(group), total_size),
        '.text={:<10} .rodata={:<10} other={:<10} total={}'.format(
            _PrettySize(code_size), _PrettySize(ro_size),
            _PrettySize(total_size - code_size - ro_size),
            _PrettySize(total_size)),
        'Number of object files: {}'.format(len(unique_paths)),
        '',
        'First columns are: running total, type, size',
    ]
    children_desc = self._DescribeSymbolGroupChildren(group)
    return itertools.chain(header_desc, children_desc)

  def _DescribeSymbolDiff(self, diff):
    header_template = ('{} symbols added (+), {} changed (~), {} removed (-), '
                       '{} unchanged ({})')
    unchanged_msg = '=' if self.verbose else 'not shown'
    symbol_delta_desc = [header_template.format(
        diff.added_count, diff.changed_count, diff.removed_count,
        diff.unchanged_count, unchanged_msg)]

    similar_paths = set()
    added_paths = set()
    removed_paths = set()
    for s in diff:
      if diff.IsAdded(s):
        added_paths.add(s.object_path)
      elif diff.IsRemoved(s):
        removed_paths.add(s.object_path)
      else:
        similar_paths.add(s.object_path)
    added_paths.difference_update(similar_paths)
    removed_paths.difference_update(similar_paths)
    path_delta_desc = ['{} object files added, {} removed'.format(
        len(added_paths), len(removed_paths))]
    if self.verbose and len(added_paths):
      path_delta_desc.append('Added files:')
      path_delta_desc.extend('  ' + p for p in sorted(added_paths))
    if self.verbose and len(removed_paths):
      path_delta_desc.append('Removed files:')
      path_delta_desc.extend('  ' + p for p in sorted(removed_paths))

    diff = diff if self.verbose else diff.WhereNotUnchanged()
    group_desc = self._DescribeSymbolGroup(diff)
    return itertools.chain(symbol_delta_desc, path_delta_desc, ('',),
                           group_desc)

  def _DescribeSizeInfoDiff(self, diff):
    common_metadata = {k: v for k, v in diff.before_metadata.iteritems()
                       if diff.after_metadata[k] == v}
    before_metadata = {k: v for k, v in diff.before_metadata.iteritems()
                       if k not in common_metadata}
    after_metadata = {k: v for k, v in diff.after_metadata.iteritems()
                      if k not in common_metadata}
    metadata_desc = itertools.chain(
        ('Common Metadata:',),
        ('    %s' % line for line in DescribeMetadata(common_metadata)),
        ('Old Metadata:',),
        ('    %s' % line for line in DescribeMetadata(before_metadata)),
        ('New Metadata:',),
        ('    %s' % line for line in DescribeMetadata(after_metadata)))
    section_desc = self._DescribeSectionSizes(diff.section_sizes)
    group_desc = self.GenerateLines(diff.symbols)
    return itertools.chain(metadata_desc, section_desc, ('',), group_desc)

  def _DescribeSizeInfo(self, size_info):
    metadata_desc = itertools.chain(
        ('Metadata:',),
        ('    %s' % line for line in DescribeMetadata(size_info.metadata)))
    section_desc = self._DescribeSectionSizes(size_info.section_sizes)
    coverage_desc = ()
    if self.verbose:
      coverage_desc = itertools.chain(
          ('',), DescribeSizeInfoCoverage(size_info))
    group_desc = self.GenerateLines(size_info.symbols)
    return itertools.chain(metadata_desc, section_desc, coverage_desc, ('',),
                           group_desc)

  def GenerateLines(self, obj):
    if isinstance(obj, models.SizeInfoDiff):
      return self._DescribeSizeInfoDiff(obj)
    if isinstance(obj, models.SizeInfo):
      return self._DescribeSizeInfo(obj)
    if isinstance(obj, models.SymbolDiff):
      return self._DescribeSymbolDiff(obj)
    if isinstance(obj, models.SymbolGroup):
      return self._DescribeSymbolGroup(obj)
    if isinstance(obj, models.Symbol):
      return self._DescribeSymbol(obj)
    return (repr(obj),)


def DescribeSizeInfoCoverage(size_info):
  """Yields lines describing how accurate |size_info| is."""
  symbols = models.SymbolGroup(size_info.raw_symbols)
  for section in models.SECTION_TO_SECTION_NAME:
    if section == 'd':
      expected_size = sum(v for k, v in size_info.section_sizes.iteritems()
                          if k.startswith('.data'))
    else:
      expected_size = size_info.section_sizes[
          models.SECTION_TO_SECTION_NAME[section]]

    def one_stat(group):
      template = ('Section %s has %.1f%% of %d bytes accounted for from '
                  '%d symbols. %d bytes are unaccounted for. Padding '
                  'accounts for %d bytes')
      actual_size = group.size
      count = len(group)
      padding = group.padding
      size_percent = 100.0 * actual_size / expected_size
      return (template % (section, size_percent, actual_size, count,
                          expected_size - actual_size, padding))

    in_section = symbols.WhereInSection(section)
    yield one_stat(in_section)

    star_syms = in_section.WhereNameMatches(r'^\*')
    attributed_syms = star_syms.Inverted().WhereHasAnyAttribution()
    anonymous_syms = attributed_syms.Inverted()
    if star_syms or anonymous_syms:
      missing_size = star_syms.size + anonymous_syms.size
      yield ('+ Without %d merge sections and %d anonymous entries ('
                  'accounting for %d bytes):') % (
          len(star_syms),  len(anonymous_syms), missing_size)
      yield '+ ' + one_stat(attributed_syms)


def _UtcToLocal(utc):
  epoch = time.mktime(utc.timetuple())
  offset = (datetime.datetime.fromtimestamp(epoch) -
            datetime.datetime.utcfromtimestamp(epoch))
  return utc + offset


def DescribeMetadata(metadata):
  display_dict = metadata.copy()
  timestamp = display_dict.get(models.METADATA_ELF_MTIME)
  if timestamp:
    timestamp_obj = datetime.datetime.utcfromtimestamp(timestamp)
    display_dict[models.METADATA_ELF_MTIME] = (
        _UtcToLocal(timestamp_obj).strftime('%Y-%m-%d %H:%M:%S'))
  gn_args = display_dict.get(models.METADATA_GN_ARGS)
  if gn_args:
    display_dict[models.METADATA_GN_ARGS] = '; '.join(gn_args)
  return sorted('%s=%s' % t for t in display_dict.iteritems())


def GenerateLines(obj, verbose=False, recursive=False):
  """Returns an iterable of lines (without \n) that describes |obj|."""
  return Describer(verbose=verbose, recursive=recursive).GenerateLines(obj)


def WriteLines(lines, func):
  for l in lines:
    func(l)
    func('\n')
