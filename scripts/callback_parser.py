import json
import os
import subprocess
from collections import defaultdict
import sys

ORANGE = '\033[38;2;255;165;0m'
RED = '\033[31m'
RESET = '\033[0m'

for_header_file = {}
lib_json = {}
struct_callbacks = {}
reachable_structs = {}
global_syms = {}
struct_metadata = {}
locals_from_structs = {}
duplicate_locals = {}
CPTR_REDIRECTION_TO_EXTERNAL = []
STRUCTS_USED_BY_CBGs = {}
PTHREAD_KEYS = False

def strip_symbol_version(symbol):
    return symbol.split('@')[0]


def parse_struct_map(filename):
    global reachable_structs
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or ':' not in line:
                continue
            lib, structs = line.split(':', 1)
            lib = lib.strip()
            struct_list = [s.strip() for s in structs.split(',') if s.strip()]
            reachable_structs[lib] = struct_list


def get_exported_symbols(so_path, is_bin = False):
    symbols = dict()
    target = ''
    try:
        if not is_bin: target = '-D'
        else: target = '-a'
        result = subprocess.run(
            ['nm', target, '--defined-only', so_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                symbol_address = parts[0]
                symbol_name = strip_symbol_version(parts[2])
                if (symbol_name) in symbols and is_bin:
                    if symbol_name not in duplicate_locals: duplicate_locals[symbol_name] = [hex(int(symbol_address,16))]
                    elif hex(int(symbol_address,16)) not in duplicate_locals[symbol_name]: duplicate_locals[symbol_name].append(hex(int(symbol_address,16)))
                    continue
                symbols[symbol_name] = hex(int(symbol_address,16))
    except Exception as e:
        print(f"Error processing {so_path}: {e}")
    return symbols

def get_exported():
    global lib_json
    with open(sys.argv[1]) as f:
        lib_json = json.load(f)

    name_to_symbols = {}
    for path, name in lib_json.items():
        exported_symbols = get_exported_symbols(path)
        name_to_symbols[path] = exported_symbols
    return name_to_symbols

def bin_parse(bin_path):
    return get_exported_symbols(bin_path, True)

def parse_sym(path, lib_dict):
    global CPTR_REDIRECTION_TO_EXTERNAL
    with open(path, "r") as f:
        for line in f:
            if line.startswith("Function"):
                parts = line.strip().split(":")
                parts = [p.strip() for p in parts]
                if len(parts) >= 3:
                    func_name = parts[1]
                    last_token = parts[-1]
                    for lib in lib_dict:
                        if last_token in lib_dict[lib] and last_token not in CPTR_REDIRECTION_TO_EXTERNAL:
                            CPTR_REDIRECTION_TO_EXTERNAL.append((func_name,lib))
            elif ':' in line:
                parts = line.strip().split(":")
                parts[-1] = parts[-1].removeprefix(" struct.")
                if parts[-1] not in STRUCTS_USED_BY_CBGs: STRUCTS_USED_BY_CBGs[parts[-1]] = [parts[0]]
                elif parts[0] not in STRUCTS_USED_BY_CBGs[parts[-1]]: STRUCTS_USED_BY_CBGs[parts[-1]].append(parts[0])
            

def parse_dynsym(path):
    global struct_callbacks
    global global_syms
    global locals_from_structs
    caller_to_callees = defaultdict(set)
    caller_to_info = defaultdict(lambda: {'callees': set(), 'indices': set()})

    with open(path) as f:
        for line in f:
            line = line.strip()
            if "Argument " in line:
                parts = [p.strip() for p in line.split(':')]
                for lib in lib_dict:
                    if parts[-2] in lib_dict[lib]:
                        print(f"{ORANGE}Transfer callback table to {lib}{RESET} cause of {parts[-2]}.\n")
                        if parts[-1] in STRUCTS_USED_BY_CBGs:
                            STRUCTS_USED_BY_CBGs[parts[-1]].append(1)
                            STRUCTS_USED_BY_CBGs[parts[-1]].append(lib)
                        break
                continue

    for struct in list(STRUCTS_USED_BY_CBGs):
        if 1 not in STRUCTS_USED_BY_CBGs[struct]: del(STRUCTS_USED_BY_CBGs[struct])

    with open(functions_file) as f:
        for line in f:
            line = line.strip()
            if not line or ':' not in line:
                continue

            elif "phi incoming" in line:
                continue

            if "pthread_key_create" in line: PTHREAD_KEYS=True

            if line.startswith("Possible Struct Callback global:"):
                parts = [p.strip() for p in line.split(':')]
                for lib in lib_dict:
                    if parts[-2] in lib_dict[lib]:
                        if lib_json[lib] not in global_syms:
                            global_syms[lib_json[lib]] = [parts[-2]]
                            print(f"{RED}NEW symbol{RESET} cause of {parts[-2]}.\n")
                        else:
                            if parts[-2] not in global_syms[lib_json[lib]]:
                                global_syms[lib_json[lib]].append(parts[-2])
                                print(f"{RED}NEW symbol{RESET} cause of {parts[-2]}.\n")
                        # if lib_json[lib] not in global_syms:
                        #     global_syms[lib_json[lib]] = [lib_json[lib] + '_' + lib_dict[lib][parts[-1]]]
                        # else:
                        #     if lib_dict[lib][parts[-1]] not in global_syms[lib_json[lib]]: global_syms[lib_json[lib]].append(lib_json[lib] + '_' + lib_dict[lib][parts[-1]])
                        break

            if False: continue
            elif "Possible Struct Callback" in line:
                parts = [p.strip() for p in line.split(':')]
                struct = parts[-2].removeprefix("struct.")
                for dso in reachable_structs:
                    if struct in reachable_structs[dso]:
                        if dso in struct_callbacks and parts[1] not in struct_callbacks[dso]:
                            struct_callbacks[dso].append(parts[1])
                        elif dso not in struct_callbacks:
                            struct_callbacks[dso] = [parts[1]]
                        continue
                if len(parts) == 4:
                    if struct in struct_metadata and parts[1] not in struct_metadata[struct]: struct_metadata[struct].append(parts[1])
                    elif struct not in struct_metadata: struct_metadata[struct] = [parts[1]]
                continue
            
            # elif "Argument " in line:
            #     parts = [p.strip() for p in line.split(':')]
            #     for lib in lib_dict:
            #         if parts[-2] in lib_dict[lib]:
            #             print(f"{ORANGE}Transfer callback table to {lib}{RESET} cause of {parts[-2]}.\n")
            #             break
            #     continue

            elif "may come from struct" in line:
                parts = [p.strip() for p in line.split(':')]
                index = parts[1].split()[-1]
                if parts[-1].removeprefix("struct.") not in locals_from_structs: locals_from_structs[parts[-1].removeprefix("struct.")] = [[parts[0],index]]
                else:
                    flag = True
                    for array in locals_from_structs[parts[-1].removeprefix("struct.")]:
                        if array[0] == parts[0] and index not in array:
                            flag = False
                            array.append(index)
                            break
                        elif array[0] == parts[0] and index in array:
                            flag = False
                            break          
                    if flag:
                        locals_from_structs[parts[-1].removeprefix("struct.")].append([parts[0],index])
                continue
            

            # if line.startswith("Possible Struct Callback global :"):
            #     parts = [p.strip() for p in line.split(':')]
            #     for lib in lib_dict:
            #         if parts[-1] in lib_dict[lib]:
            #             if lib_json[lib] not in global_syms:
            #                 global_syms[lib_json[lib]] = [parts[-1]]
            #             else:
            #                 if parts[-1] not in global_syms[lib_json[lib]]: global_syms[lib_json[lib]].append(parts[-1])
            #             # if lib_json[lib] not in global_syms:
            #             #     global_syms[lib_json[lib]] = [lib_json[lib] + '_' + lib_dict[lib][parts[-1]]]
            #             # else:
            #             #     if lib_dict[lib][parts[-1]] not in global_syms[lib_json[lib]]: global_syms[lib_json[lib]].append(lib_json[lib] + '_' + lib_dict[lib][parts[-1]])
            #             print(f"{RED}NEW symbol{RESET} cause of {parts[-1]}.\n")
            #             break
            #     continue

            caller, callee = map(str.strip, line.split(':', 1))
            callee = callee.split()
            index = callee[-1]
            callee = callee[0]
            assert index != callee
            if caller == "atexit": caller = "__cxa_atexit"
            caller_to_info[caller]['callees'].add(callee)
            caller_to_info[caller]['indices'].add(index)
    return caller_to_info

def check(call_dict, lib_dict, bin_dict, plt_addrs, name):

    for item in call_dict:
        found = False
        library = ''
     
        for lib in lib_dict:
            if item in lib_dict[lib]:   
                print(f"\nItem to insert in callb_getters: {item}, lib: {lib}, positions: {call_dict[item]['indices']}")
                found = True
                library = lib
                break

        if found:
            print(call_dict[item]["callees"])
            for callee in call_dict[item]['callees']:
                #if callee == "alphasort64": continue
                callee_found = False
                for lib in lib_dict:
                    if callee in lib_dict[lib]:
                        print(f"Found {callee} in: {lib}")
                        callee_found = True
                        if lib_json[library] in for_header_file and (name + '_' + plt_addrs[callee]) not in for_header_file[lib_json[library]]: for_header_file[lib_json[library]].append(name + '_' + plt_addrs[callee])
                        elif lib_json[library] not in for_header_file:
                            for_header_file[lib_json[lib]] = [name + '_' + plt_addrs[callee]]
                        break
                if not callee_found:
                    if callee in bin_dict:
                        print(f"Found {callee} in: {sys.argv[3]} {bin_dict[callee]}")
                        if lib_json[library] in for_header_file and bin_dict[callee] not in for_header_file[lib_json[library]]: for_header_file[lib_json[library]].append(name + '_' + bin_dict[callee])
                        elif lib_json[library] not in for_header_file:
                            for_header_file[lib_json[library]] = [name + '_' + bin_dict[callee]]
                        callee_found = True
                        if callee in duplicate_locals:
                            for duplicate in duplicate_locals[callee]:
                                for_header_file[lib_json[library]].append(name + '_' + duplicate)

                        # LTO appends .<num> to duplicate syms
                        # We do not use LTO but since redis compiles with it by default
                        # we need to also add these duplicate symbols
                        # For full LTO support some logic should be added in this script

                        for k in bin_dict:
                            if k.startswith(f"{callee}.") and (name + '_' + bin_dict[k]) not in for_header_file[lib_json[library]]:
                                for_header_file[lib_json[library]].append(name + '_' + bin_dict[k])
                                #print(name + '_' + bin_dict[k])
                                                    


                if not callee_found:
                    print("Symbol not found: ", callee)
        
        for tup in CPTR_REDIRECTION_TO_EXTERNAL:
            if item in tup:
                for func in call_dict[item]['callees']:
                    if func in bin_dict and lib_json[tup[1]] not in for_header_file: for_header_file[lib_json[tup[1]]]= [name + '_' + bin_dict[func]]
                    elif func in bin_dict and (name + '_' + bin_dict[func]) not in for_header_file[lib_json[tup[1]]]: for_header_file[lib_json[tup[1]]].append(name + '_' + bin_dict[func])
                break

        for structs in STRUCTS_USED_BY_CBGs:
            func_list = STRUCTS_USED_BY_CBGs[structs]
            if item in func_list:
                for func in call_dict[item]['callees']:
                    if func in bin_dict and lib_json[func_list[-1]] not in for_header_file: for_header_file[lib_json[func_list[-1]]]= [name + '_' + bin_dict[func]]
                    elif func in bin_dict and (name + '_' + bin_dict[func]) not in for_header_file[lib_json[func_list[-1]]]: for_header_file[lib_json[func_list[-1]]].append(name + '_' + bin_dict[func])
                break


    for dso in struct_callbacks:
        for function in struct_callbacks[dso]:
            if function in plt_addrs:
                if dso in for_header_file:
                    if (name + '_' + plt_addrs[function]) not in for_header_file[dso]:
                        print(function, name + '_' + plt_addrs[function])
                        for_header_file[dso].append(name + '_' + plt_addrs[function])
                        if ("BothPLTs_" + function) in plt_addrs:
                            for_header_file[dso].append(name + '_' + plt_addrs["BothPLTs_" + function])
                else:
                    for_header_file[dso] = [name + '_' + plt_addrs[function]]
                    if ("BothPLTs_" + function) in plt_addrs:
                        for_header_file[dso].append(name + '_' + plt_addrs["BothPLTs_" + function])


            else:    
                for lib in lib_dict:
                    if function in lib_dict[lib]:
                        if dso in for_header_file:
                            if (dso + '_' + lib_dict[lib][function]) not in for_header_file[dso]: for_header_file[dso].append(dso + '_' + lib_dict[lib][function])
                        else:
                            for_header_file[dso] = [dso + '_' + lib_dict[lib][function]]

                        break
        
                if function in bin_dict:
                    if dso in for_header_file:
                        if (name + bin_dict[function]) not in for_header_file[dso]: for_header_file[dso].append(name + '_' + bin_dict[function])
                    else:
                        for_header_file[dso] = [name + '_' + bin_dict[function]]
    
    for dso in global_syms:
        if dso not in for_header_file:
            for_header_file[dso] = []
        for function in global_syms[dso]:
            if function not in plt_addrs:
                print(function)
                continue
            if (name + '_' + plt_addrs[function]) not in for_header_file[dso]:
                for_header_file[dso].append(name + '_' + plt_addrs[function])
    
    for struct_name in struct_metadata:
        if struct_name not in locals_from_structs:
            continue
        for fn_call_tuple in locals_from_structs[struct_name]:
            fn_name = fn_call_tuple[0]
            indices = fn_call_tuple[1:]

            if fn_name not in call_dict:
                continue

            matched_indices = set(indices) & set(call_dict[fn_name]['indices'])
            if not matched_indices:
                continue

            for lib, lib_fns in lib_dict.items():
                if fn_name in lib_fns:
                    for function in struct_metadata[struct_name]:
                        if function in bin_dict and (name + '_' + bin_dict[function]) not in for_header_file[lib_json[lib]]: 
                            print(f"{ORANGE}[+] Inserting callback {function} from \"may struct\"!{RESET}")
                            for_header_file[lib_json[lib]].append(name + '_' + bin_dict[function])
                        elif function in call_dict:
                            print("THIS IS NOT YET SUPPORTED")
                            exit()


if __name__ == '__main__':
    if len(sys.argv) != 8:
        print(f"Usage: python {sys.argv[0]} <json> <dynsym> <ELF> <DSO_or_bin> <name> <reachable_structs> <sym>")
        sys.exit(1)

    lib_dict = get_exported()
    bin_dict = bin_parse(sys.argv[3])
    #print(bin_dict)
    parse_struct_map(sys.argv[6])
    functions_file = sys.argv[2]

    import plt_addr
    plt_addrs = plt_addr.main(sys.argv[3])

    parse_sym(sys.argv[7], lib_dict)

    call_calle = parse_dynsym(functions_file)

    check(call_calle, lib_dict, bin_dict, plt_addrs, sys.argv[5])

    from create_tables import main as init_fini_functions
    init_fini = init_fini_functions(sys.argv[3], sys.argv[5])

    if sys.argv[4] == '1':
        if "LIBC" in for_header_file:
            for_header_file["LIBC"] += init_fini[0]
        else:
            for_header_file["LIBC"] = init_fini[0]
        
    for_header_file["FINI"] = init_fini[1]
    
    if PTHREAD_KEYS:
        print(f"{RED}Pthread Key Destructor needs to be included{RESET}")
        print("Not automated yet :(\n Need to check in the check() function in each case")
    else:
        for_header_file["THREADKEY"] = []
    print(for_header_file)