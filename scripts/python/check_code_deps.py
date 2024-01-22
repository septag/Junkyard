import argparse
import sys
import os
import glob

FOLDER_EXCEPTIONS = ['External', 'Tests']
FILE_EXTENSIONS = ['.cpp', '.h', '.hpp', '.c']
INCLUDE_PHRASE = '#include "'
HAS_ERRORS:bool = False

def get_dependencies(root_dir, module_name):
    directory = os.path.join(root_dir, module_name)
    module_dep_filepath = os.path.join(directory, 'DEPENDS')

    deps = []
    if os.path.isfile(module_dep_filepath):
        with open(module_dep_filepath, 'rt') as f:
            deps = f.readlines()
            deps[:] = [s.strip() for s in deps]

    return deps

def check_dependency_for_module(root_dir, module_name):
    global HAS_ERRORS
    print('Checking module:', module_name)
    directory = os.path.join(root_dir, module_name)
    module_dep_filepath = os.path.join(directory, 'DEPENDS')
    
    deps = get_dependencies(root_dir, module_name)
    if len(deps):
        print('\tDependencies:', deps)

    # create full path and check circular dependencies
    # check for circular dependencies
    dep_dirs = []
    for d in deps:
        if d:
            child_deps = get_dependencies(root_dir, d)
            if module_name in child_deps:
                print('\tError: Circular depepdency found with module "{}"'.format(d))
                HAS_ERRORS = True

            dpath = os.path.join(root_dir, d)
            if os.path.isdir(dpath):
                dep_dirs.append(os.path.normpath(dpath))

    # only gather files at the top level of the module. We don't keep source files in sub-folders (hopefully)
    files_to_check = []
    for ext in FILE_EXTENSIONS:
        files_to_check.extend(glob.glob('*' + ext, root_dir=directory, recursive=False))

    errors = []
    # remove all items with 
    for source_file in files_to_check:
        with open(os.path.join(directory, source_file), 'rt') as f:
            lines = f.readlines()
            for lineno, line in enumerate(lines):
                line = line.strip()
                if line and line.startswith(INCLUDE_PHRASE):
                    start_idx = len(INCLUDE_PHRASE)
                    end_idx = line.find('"', start_idx)
                    if end_idx != -1:
                        include_path = line[start_idx:end_idx]
                        full_include_path = os.path.abspath(os.path.join(directory, include_path))
                        if not full_include_path.startswith(directory):
                            # TODO: We have to remove this exception. No module should have access to the main source files in root folder
                            found_in_deps = True if os.path.dirname(full_include_path) == root_dir else False

                            # 'Core' cannot access the root dir source code
                            if module_name == 'Core':
                                found_in_deps = False
                                                                                   
                            if not found_in_deps:
                                for dep_dir in dep_dirs:
                                    if full_include_path.startswith(dep_dir):
                                        found_in_deps = True
                                        break
                            if not found_in_deps:
                                errors.append((line, source_file, lineno+1))

    if len(errors):
        print('\tFound', len(errors), 'errors:')
        HAS_ERRORS = True
        for e in errors:
            print('\t\t{}: {} - line: {}'.format(e[0], e[1], e[2]))

arg_parser = argparse.ArgumentParser(description='')
arg_parser.add_argument('--codedir', help='Root of the code directory', required=True)
args = arg_parser.parse_args(sys.argv[1:])

if not os.path.isdir(args.codedir):
    print('Error:', 'code directory is invalid:', args.codedir)

code_dir = os.path.normpath(os.path.abspath(args.codedir))

for root, dirs, files in os.walk(code_dir):
    if root == code_dir:
        for d in dirs:
            if d[:1] != '.' and d not in FOLDER_EXCEPTIONS:
                check_dependency_for_module(root, d)

if HAS_ERRORS:
    exit(-1)
else:
    print('All seems to be good!')


