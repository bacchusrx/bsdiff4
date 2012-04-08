#!/usr/bin/env python

def set_options(ctx):
  ctx.tool_options('compiler_cxx')

def configure(ctx):
  ctx.check_tool('compiler_cxx')
  ctx.check_tool('node_addon')

def build(ctx):
  t = ctx.new_task_gen('cxx', 'shlib', 'node_addon')

  t.source = ['src/bsdiff4.cc']

  # Must be same as first parameter in NODE_MODULE.
  t.target = 'bsdiff4'
