project('depthdeck',
  'cpp',
  version: '0.1.0',
  default_options: ['cpp_std=c++20', 'warning_level=2'])

# wayfire.pc (pacote extra/wayfire) traz headers públicos + wlroots + wf-config.
wf = dependency('wayfire', version: '>=0.11')

shared_module('depthdeck',
  'src/depthdeck.cpp',
  dependencies: [wf],
  install: false)
