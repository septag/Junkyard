#!/usr/bin/env python

import argparse
import sys
import os
import glob

BASE_HEADER = 'Base.h'
INCLUDE_PHRASE = '#include "'
ONCE_PRAGMA = '#pragma once'
EXCLUDE_EXTERNALS = ['MemPro', 'Tracy']
IGNORE_MODULES = ['config', 'debug', 'atomic', 'includewin', 'tracyhelper']
IGNORE_HEADERS = ['TargetConditionals.h']   # used as double-quotes in slang
LINE_WIDTH = 120

arg_parser = argparse.ArgumentParser(description='')
arg_parser.add_argument('--rootdir', help='Root of the code directory (default=cwd)', default='../../code/Core')
arg_parser.add_argument('--stbheader', help='Make stb style single header', action='store_true', default=False)
arg_parser.add_argument('--outputname', help='Output name of the output cpp/h files (default=Core)', default='Core')
arg_parser.add_argument('--outputdir', help='Output directory path to write cpp/h files (default=cwd)', default='.')
arg_parser.add_argument('--verbose', help='Output verbose info', action='store_true', default=False)
arg_parser.add_argument('--ignore-comment-lines', help='Do not inline lines that start with comment "//"', action='store_true', default=False)
arg_parser.add_argument('modules', help='Name of the core library parts. Corresponds to header file names (w/o extension)', nargs='*')
args = arg_parser.parse_args(sys.argv[1:])

rootdir = os.path.abspath(args.rootdir)

if not os.path.isdir(rootdir):
    print('Error:', 'code directory is invalid:', rootdir)
    exit(-1)

def file_in_exclude_externals(filepath:str):
    root, ext = os.path.splitext(filepath)
    ext = ext.lower()

    # only consider source files to be externals, not the headers
    if ext == '.cpp' or ext == '.c':
        filedir = os.path.dirname(filepath).lower()
        for extern in EXCLUDE_EXTERNALS:
            if filedir.endswith(extern.lower()):
                return True
    return False

def parse_include_directive(line:str):
    if not line:
        return None
    
    if not line.startswith('#'):
        return None
    else:
        line = '#' + line[1:].lstrip()
        
    if line.startswith(INCLUDE_PHRASE):
        start_idx = len(INCLUDE_PHRASE)
        end_idx = line.find('"', start_idx)
        if end_idx != -1:
            include_path = line[start_idx:end_idx]
            if include_path not in IGNORE_HEADERS:
                return include_path
            else:
                return None
    
    return None

def preprocess_file(filepath, included_files):
    filepath = os.path.abspath(filepath)
    if filepath not in included_files:
        included_files.add(filepath.lower())

    base_dir = os.path.dirname(filepath)
    lines = []
    with open(filepath, 'rt') as file:
        lines = file.readlines()

    blob:str = ''
    for line in lines:
        stripped_line:str = line.strip()
        if stripped_line.startswith(ONCE_PRAGMA):
            continue
        if args.ignore_comment_lines and stripped_line.startswith('//'):
            continue

        header_filepath = parse_include_directive(stripped_line)
        if header_filepath and not file_in_exclude_externals(header_filepath):
            header_fullpath = os.path.abspath(os.path.join(base_dir, header_filepath))
            if header_fullpath.lower() not in included_files:
                if args.verbose:
                    print(header_fullpath, 'included by:', filepath)
                content = preprocess_file(header_fullpath, included_files)
                if content:
                    blob = blob + ('//' + '-'*(LINE_WIDTH-2) + '\n')
                    blob = blob + ('// ' + header_filepath + '\n')
                    blob = blob + content                
        else:
            blob = blob + line
    return blob

header_blob:str = ''
source_blob:str = ''
included_files = set()

modules = args.modules
if len(modules) == 0:
    print('No modules are set. Unifying all modules.')
    header_files = glob = glob.glob('*.h', root_dir=args.rootdir)
    for header_file in header_files:
        if header_file != (args.outputname + '.h'):
            rootname, ext = os.path.splitext(header_file)
            modules.append(rootname)

for index, module in enumerate(modules):
    if module.lower() in IGNORE_MODULES:
        print 
        del modules[index]
assert len(modules) > 0

if args.verbose:
    print('Modules:', modules)

# headers
for module in modules:
    module_include_path = os.path.abspath(os.path.join(args.rootdir, module + '.h'))
    if os.path.isfile(module_include_path) and module_include_path.lower() not in included_files:
        header_blob = header_blob + preprocess_file(module_include_path, included_files)

# sources
for module in modules:            
    module_source_path = os.path.abspath(os.path.join(args.rootdir, module + '.cpp'))
    if os.path.isfile(module_source_path):
        source_blob = source_blob + preprocess_file(module_source_path, included_files)

# for each included file, check if we have the source file, then include them into the cpp
included_files_in_header = included_files.copy()
for include_file in included_files_in_header:
    root, ext = os.path.splitext(include_file)
    source_filepath = root + '.cpp'
    if os.path.isfile(source_filepath) and source_filepath not in included_files:
        source_blob = source_blob + preprocess_file(source_filepath, included_files)

output_header_path = os.path.abspath(os.path.join(args.outputdir, args.outputname + '.h'))
output_source_path = os.path.abspath(os.path.join(args.outputdir, args.outputname + '.cpp'))
with open(output_header_path, 'wt') as f:
    f.write('// This header file is auto-generated\n')
    f.write('// Inlined files:\n')
    for filepath in included_files_in_header:
        f.write('//\t' + os.path.basename(filepath) + '\n')
    f.write('\n')
    f.write('#pragma once\n')
    f.write(header_blob)
    if args.stbheader:
        f.write('\n//' + '-'*(LINE_WIDTH-2) + '\n')
        f.write('// ' + args.outputname + ' implementation\n')
        f.write('#ifdef ' + args.outputname.upper() + '_IMPLEMENT\n')
        f.write('#define BUILD_UNITY\n')
        f.write(source_blob)
        f.write('\n#endif // ' + args.outputname.upper() + '_IMPLEMENT\n')
    print('Generated file:', output_header_path)

if not args.stbheader:
    with open(output_source_path, 'wt') as f:
        f.write('// This source file is auto-generated\n')
        for filepath in included_files:
            if filepath not in included_files_in_header:
                f.write('//\t' + os.path.basename(filepath) + '\n')
        f.write('\n')
        f.write('#include "{}.h"\n'.format(args.outputname))
        f.write(source_blob)
    print('Generated file:', output_source_path)
