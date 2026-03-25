import sys
import os
import json
import clang.cindex

# ----- Change this as needed for your system -----
clang.cindex.Config.set_library_file("/usr/lib/llvm-20/lib/libclang-20.so.1")
# -------------------------------------------------

ANNOTATION = '__attribute__((annotate("callback_maybe")))'

def is_funcptr_type(t, depth=0):
    canonical = t.get_canonical()
    if canonical.kind == clang.cindex.TypeKind.POINTER:
        p = canonical.get_pointee()
        if p.kind in (clang.cindex.TypeKind.FUNCTIONPROTO, clang.cindex.TypeKind.FUNCTIONNOPROTO):
            return True
    if t.kind == clang.cindex.TypeKind.TYPEDEF:
        return is_funcptr_type(canonical, depth+1)
    if t.kind == clang.cindex.TypeKind.ELABORATED:
        return is_funcptr_type(canonical, depth+1)
    return False

def get_compile_args(compile_commands_json_path, srcfile):
    srcfile_abs = os.path.abspath(srcfile)
    with open(compile_commands_json_path) as f:
        cmds = json.load(f)
    entry = None
    for e in cmds:
        file_abs = os.path.abspath(os.path.join(e['directory'], e['file']))
        if file_abs == srcfile_abs:
            entry = e
            break
    if entry is None:
        raise Exception(f"{srcfile} not found in {compile_commands_json_path}")
    args = entry.get('arguments', [])
    if not args:
        args = entry['command'].split()[1:]
    else:
        args = args[1:]
    clean_args = []
    skip = False
    for a in args:
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a.endswith('.c') or a.endswith('.cc') or a.endswith('.cpp'):
            continue
        clean_args.append(a)
    return clean_args

def find_funcptr_arg_functions(tu, target_filename):
    candidates = []
    abs_target = os.path.abspath(target_filename)
    for node in tu.cursor.walk_preorder():
        if node.kind == clang.cindex.CursorKind.FUNCTION_DECL and node.is_definition():

            if node.location.file and os.path.abspath(node.location.file.name) == abs_target:
                for arg in node.get_arguments():
                    if is_funcptr_type(arg.type):
                        print(f"[INFO] found: {node.spelling} at {node.location.file}:{node.location.line}")
                        candidates.append(node)
                        break
    print(f"[INFO] Found {len(candidates)} matching functions in {os.path.basename(target_filename)}")
    return candidates

def already_has_annotation(header_line, src_lines):

    L = max(0, header_line - 4)
    U = header_line - 1
    for l in range(L, U):
        if ANNOTATION in src_lines[l]:
            return True
    return False

def looks_like_function_declaration(line):
    stripped = line.strip()
    if (not stripped) or stripped.startswith("#"):
        return False

    if "(" in stripped and stripped.split("(")[0].isupper():
        return False

    return True

def annotate_functions_in_file(filename, func_nodes, dry_run=False):
    with open(filename, 'r') as f:
        lines = f.readlines()
    insert_points = set()
    for node in func_nodes:
        insert_line = node.extent.start.line
        if already_has_annotation(insert_line, lines):
            print(f"[SKIP] {node.spelling} at line {insert_line} (already annotated)")
            continue

        if not looks_like_function_declaration(lines[insert_line-1]):
            continue

        if insert_line in insert_points:
            print(f"[SKIP] {node.spelling} at line {insert_line} (already will annotate here)")
            continue
        insert_points.add(insert_line)
        print(f"[NOTE] Will annotate {node.spelling} at line {insert_line} in {os.path.basename(filename)}")
    if not insert_points:
        print(f"[INFO] No changes needed in {os.path.basename(filename)}")
        return 0

    for insert_line in sorted(insert_points, reverse=True):
        lines.insert(insert_line-1, ANNOTATION+'\n')
    if not dry_run:
        with open(filename, 'w') as f:
            f.writelines(lines)
    print(f"[INFO] Annotated {len(insert_points)} functions in {os.path.basename(filename)}")
    return len(insert_points)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <c-file> <compile_commands.json> [logfile]", file=sys.stderr)
        sys.exit(1)
    srcfile = os.path.abspath(sys.argv[1])
    compile_commands_path = sys.argv[2]
    logfilename = sys.argv[3] if len(sys.argv) >= 4 else None

    index = clang.cindex.Index.create()
    try:
        clean_args = get_compile_args(compile_commands_path, srcfile)
    except Exception as e:
        print(f"[INFO] {e}")
        sys.exit(2)

    tu = index.parse(srcfile, args=clean_args, options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    nodes_to_annotate = find_funcptr_arg_functions(tu, srcfile)

    if not nodes_to_annotate:
        print(f"[INFO] No functions with function pointer arguments found in {os.path.basename(srcfile)}")
        sys.exit(0)

    cnt = annotate_functions_in_file(srcfile, nodes_to_annotate)
    if logfilename and cnt:
        try:
            with open(logfilename, "a") as logf:
                for nd in nodes_to_annotate:
                    logf.write(f"{nd.spelling}\n")
        except Exception as e:
            print(f"Logfile error: {e}", file=sys.stderr)