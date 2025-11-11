import sys

def parse_struct_names_from_file(filename):
    structs = set()
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()

            if 'struct.' in line:
                idx = line.find('struct.')
                remainder = line[idx+len('struct.'):]
                remainder = remainder.split()[0]
                structs.add(remainder)
    return structs

def emit_cpp_unordered_set(structs):
    print('#include <unordered_set>')
    print('#include <string>')
    print()
    print('extern const std::unordered_set<std::string> struct_names = {')
    for name in sorted(structs):
        print(f'    "{name}",')
    print('};')

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input_file>", file=sys.stderr)
        sys.exit(1)
    filename = sys.argv[1]
    structs = parse_struct_names_from_file(filename)
    emit_cpp_unordered_set(structs)
