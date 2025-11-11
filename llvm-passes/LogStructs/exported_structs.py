import sys

exported_structs = []

def parse(filename):
    with open(filename, 'r', encoding='utf-8') as file:
        for line in file:
            struct = line.split(':')[1].strip().split()[0]
            if struct not in exported_structs:
                exported_structs.append(struct)
    print("Total number of exported structs: ", len(exported_structs))
    for i in range(len(exported_structs)):
        print(exported_structs[i])

if __name__ == "__main__":
    filename = sys.argv[1]
    parse(filename)