import ast
import sys
import re
from collections import defaultdict

HEADER_EXTRA = """\
extern long long int or_mask __attribute__((visibility("hidden")));
int main();
extern void *_DYNAMIC;
"""

C_PREAMBLE = """\
#define _GNU_SOURCE
"""

C_EXTRA = """\
void * MB __attribute__((visibility("default"))) = &_DYNAMIC;
"""

def parse_all_dicts_from_file(filename):
    with open(filename, 'r') as f:
        lines = f.readlines()

    first_line_data = ast.literal_eval(lines[0].strip())
    content = ''.join(lines[1:])

    possible_dicts = re.findall(r"\{.*?\}", content, re.DOTALL)
    result = {}
    for dstr in possible_dicts:
        d = ast.literal_eval(dstr)
        for k, v in d.items():
            if k not in result:
                result[k] = []
            result[k].extend(v)

    for k, v in result.items():
        result[k] = list(dict.fromkeys(v))

    for item in first_line_data:
        result[item] += result[first_line_data[item]]

    return result

def make_varname_suffix(varname, occurrence):
    if occurrence == 1:
        return f"{varname}_1337"
    return f"{varname}_{'1' * (occurrence - 1)}1337"

def c_string_literal(s: str) -> str:
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def emit_special_fini_callback(cf, fallthrough_libs):
    cf.write(
        "#include <dlfcn.h>\n"
        "#include <string.h>\n\n"
    )

    cf.write("static const char *fini_passthrough_dsos[] = {\n")
    for lib in fallthrough_libs:
        cf.write(f"    {c_string_literal(lib)},\n")
    cf.write("    0x0\n};\n\n")

    cf.write(
        "__attribute__((used, noinline))\n"
        "int fini_should_passthrough(void *addr) {\n"
        "    Dl_info info;\n"
        "    if (!dladdr(addr, &info) || !info.dli_fname)\n"
        "        return 0;\n\n"
        "    for (int i = 0; fini_passthrough_dsos[i]; ++i) {\n"
        "        if (strstr(info.dli_fname, fini_passthrough_dsos[i]) != NULL)\n"
        "            return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n"
        "__attribute__((naked))\n"
        "void fini_callback_table(unsigned long long int r11) {\n"
        "    __asm__ __volatile__ (\n"
        "        \"movq %%r11, %%xmm15\\n\\t\"\n"
        "        \"movq %%r11, %%rdi\\n\\t\"\n"
        "        \"call fini_should_passthrough\\n\\t\"\n"
        "        \"movq %%xmm15, %%r11\\n\\t\"\n"
        "        \"test %%eax, %%eax\\n\\t\"\n"
        "        \"jnz .call_r11_direct_fini\\n\\t\"\n"
        "        \"leaq fini_table(%%rip), %%rax\\n\\t\"\n"
        "        \"jmp .loop_cetfake\\n\\t\"\n"
        "        \".call_r11_direct_fini:\\n\\t\"\n"
        "        \"jmp *%%r11\\n\\t\"\n"
        "        ::: \"rax\", \"rdi\", \"xmm15\", \"memory\"\n"
        "    );\n"
        "}\n\n"
    )

def emit_libc_fallthrough_helper(cf, libc_fallthrough_libs):
    if not libc_fallthrough_libs:
        return

    cf.write(
        "#include <dlfcn.h>\n"
        "#include <string.h>\n\n"
    )

    cf.write("static const char *libc_passthrough_dsos[] = {\n")
    for lib in libc_fallthrough_libs:
        cf.write(f"    {c_string_literal(lib)},\n")
    cf.write("    0x0\n};\n\n")

    cf.write(
        "__attribute__((used, noinline))\n"
        "int libc_should_passthrough(void *addr) {\n"
        "    Dl_info info;\n"
        "    if (!dladdr(addr, &info) || !info.dli_fname)\n"
        "        return 0;\n\n"
        "    for (int i = 0; libc_passthrough_dsos[i]; ++i) {\n"
        "        if (strstr(info.dli_fname, libc_passthrough_dsos[i]) != NULL)\n"
        "            return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n"
    )
    
def emit_callback_functions(cf, mask_dict, fallthrough_libs=None, libc_fallthrough_libs=None):
    all_prefixes = list(mask_dict.keys())

    if libc_fallthrough_libs:
        emit_libc_fallthrough_helper(cf, libc_fallthrough_libs)

    for prefix in all_prefixes:
        table_name = prefix.lower() + "_table"

        if prefix == "LIBC":
            if libc_fallthrough_libs:
                cf.write(
                    "__attribute__((naked))\n"
                    "void callback_table(unsigned long long int r11) {\n"
                    "    __asm__ __volatile__ (\n"
                    "        \"leaq libc_table(%%rip), %%rax\\n\\t\"\n"
                    "        \".loop_cetfake:\\n\\t\"\n"
                    "        \"cmpq (%%rax), %%r11\\n\\t\"\n"
                    "        \"je .call_r11_cetfake\\n\\t\"\n"
                    "        \"addq $8, %%rax\\n\\t\"\n"
                    "        \"cmpq $0, (%%rax)\\n\\t\"\n"
                    "        \"jne .loop_cetfake\\n\\t\"\n"
                    "        \"movq %%r11, %%xmm15\\n\\t\"\n"
                    "        \"pushq %%rdi\\n\\t\"\n"
                    "        \"pushq %%rsi\\n\\t\"\n"
                    "        \"pushq %%rdx\\n\\t\"\n"
                    "        \"movq %%r11, %%rdi\\n\\t\"\n"
                    "        \"call libc_should_passthrough\\n\\t\"\n"
                    "        \"popq %%rdx\\n\\t\"\n"
                    "        \"popq %%rsi\\n\\t\"\n"
                    "        \"popq %%rdi\\n\\t\"\n"
                    "        \"movq %%xmm15, %%r11\\n\\t\"\n"
                    "        \"test %%eax, %%eax\\n\\t\"\n"
                    "        \"jnz .call_r11_cetfake\\n\\t\"\n"
                    "        \"hlt\\n\\t\"\n"
                    "        \".call_r11_cetfake:\\n\\t\"\n"
                    "        \"jmp *%%r11\\n\\t\"\n"
                    "        ::: \"rax\", \"rdi\", \"rsi\", \"rdx\", \"xmm15\", \"memory\"\n"
                    "    );\n"
                    "}\n\n"
                )
            else:
                cf.write(
                    "__attribute__((naked))\n"
                    "void callback_table(unsigned long long int r11) {\n"
                    "    __asm__ __volatile__ (\n"
                    "        \"leaq libc_table(%%rip), %%rax\\n\\t\"\n"
                    "        \".loop_cetfake:\\n\\t\"\n"
                    "        \"cmpq (%%rax), %%r11\\n\\t\"\n"
                    "        \"je .call_r11_cetfake\\n\\t\"\n"
                    "        \"addq $8, %%rax\\n\\t\"\n"
                    "        \"cmpq $0, (%%rax)\\n\\t\"\n"
                    "        \"jne .loop_cetfake\\n\\t\"\n"
                    "        \"hlt\\n\\t\"\n"
                    "        \".call_r11_cetfake:\\n\\t\"\n"
                    "        \"jmp *%%r11\\n\\t\"\n"
                    "        ::: \"rax\"\n"
                    "    );\n"
                    "}\n\n"
                )
        elif prefix == "FINI" and fallthrough_libs:
            emit_special_fini_callback(cf, fallthrough_libs)
        else:
            funcname = f"{prefix.lower()}_callback_table"
            cf.write(
                "__attribute__((naked))\n"
                f"void {funcname}(unsigned long long int r11) {{\n"
                "    __asm__ __volatile__ (\n"
                f"        \"leaq {table_name}(%%rip), %%rax\\n\\t\"\n"
                "        \"jmp .loop_cetfake\\n\\t\"\n"
                "        ::: \"rax\"\n"
                "    );\n"
                "}\n\n"
            )

def emit_placeholder_functions_in_c(cf, placeholders):
    if not placeholders:
        return

    first = placeholders[0]
    cf.write(f'''
__attribute__((naked, visibility("default"))) void {first}(void) {{
    __asm__ __volatile__("ret");
}}
''')

    for ph in placeholders[1:]:
        cf.write(f'''
__attribute__((visibility("default"))) void {ph}(void) __attribute__((alias("{first}")));
''')

def emit_dsym_externs_in_h(hf, dsyms):
    done = set()
    for dsym in dsyms:
        if not dsym.startswith("dsym_"):
            continue
        symbol = dsym[5:]
        if len(symbol) >= 2 and symbol not in done:
            hf.write(f"extern void {symbol}(void);\n")
            done.add(symbol)

def emit_dsym_naked_jmp_c(cf, dsyms):
    if not dsyms:
        return

    for dsym in dsyms:
        if not dsym.startswith("dsym_"):
            continue
        symbol = dsym[5:]
        if len(symbol) < 2:
            continue
        alias = "dsym_" + symbol[:-1]
        target_symbol = symbol
        cf.write(
            f'__attribute__((naked, visibility("default"))) void {alias}(void) {{\n'
            f'    __asm__("jmp {target_symbol}");\n'
            f'}}\n\n'
        )

def generate_files(input_filename, placeholders, dsyms, fallthrough_libs=None, libc_fallthrough_libs=None):
    header_file = "mask.h"
    source_file = "mask.c"

    try:
        mask_dict = parse_all_dicts_from_file(input_filename)
    except Exception as e:
        print(f"Error reading or parsing dictionary from {input_filename}: {e}", file=sys.stderr)
        sys.exit(1)

    all_vars = set()
    for values in mask_dict.values():
        all_vars.update(values)

    max_occurrence_per_symbol = defaultdict(int)
    for _, values in mask_dict.items():
        uniq_values = list(dict.fromkeys(values))
        for varname in uniq_values:
            max_occurrence_per_symbol[varname] += 1

    with open(header_file, "w") as hf:
        hf.write(HEADER_EXTRA + "\n")
        for varname in all_vars:
            for i in range(1, max_occurrence_per_symbol[varname] + 1):
                hf.write(f"extern unsigned long long int {make_varname_suffix(varname, i)};\n")
        emit_dsym_externs_in_h(hf, dsyms)

    with open(source_file, "w") as cf:
        cf.write(C_PREAMBLE + "\n")
        cf.write(f'#include "{header_file}"\n\n')
        cf.write(C_EXTRA + "\n")

        symbol_emission_counter = defaultdict(int)
        for key, values in mask_dict.items():
            table_name = key.lower() + "_table"
            uniq_values = list(dict.fromkeys(values))
            cf.write(f"void * {table_name}[] = {{\n")
            for varname in uniq_values:
                symbol_emission_counter[varname] += 1
                cf.write(f"    &{make_varname_suffix(varname, symbol_emission_counter[varname])},\n")
            if key == "LIBC":
                cf.write("    &main,\n")
            cf.write("    0x0\n};\n\n")

        emit_callback_functions(
            cf,
            mask_dict,
            fallthrough_libs=fallthrough_libs,
            libc_fallthrough_libs=libc_fallthrough_libs
        )
        emit_placeholder_functions_in_c(cf, placeholders)
        emit_dsym_naked_jmp_c(cf, dsyms)

    print(f"Generated {header_file} and {source_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(
            f"Usage: {sys.argv[0]} <input_dict.txt> [placeholder1 ...] [dsym_* ...] "
            "[fallthrough <lib_substring1> ... [libc_fallthrough <lib_substring1> ...]] "
            "[libc_fallthrough <lib_substring1> ...]"
        )
        sys.exit(1)

    input_filename = sys.argv[1]
    extra_args = sys.argv[2:]

    fallthrough_libs = []
    libc_fallthrough_libs = []

    if "fallthrough" in extra_args:
        idx = extra_args.index("fallthrough")
        pre = extra_args[:idx]
        post = extra_args[idx + 1:]

        if "libc_fallthrough" in post:
            idx2 = post.index("libc_fallthrough")
            fallthrough_libs = post[:idx2]
            libc_fallthrough_libs = post[idx2 + 1:]
        else:
            fallthrough_libs = post

        extra_args = pre
    elif "libc_fallthrough" in extra_args:
        idx = extra_args.index("libc_fallthrough")
        libc_fallthrough_libs = extra_args[idx + 1:]
        extra_args = extra_args[:idx]

    placeholders = [arg for arg in extra_args if arg.startswith("placeholder")]
    dsyms = [arg for arg in extra_args if arg.startswith("dsym_")]

    generate_files(
        input_filename,
        placeholders,
        dsyms,
        fallthrough_libs=fallthrough_libs,
        libc_fallthrough_libs=libc_fallthrough_libs
    )