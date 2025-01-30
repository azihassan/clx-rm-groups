# clx-rm-groups

Command-line tool to remove specific groups from a CLX file.

## Building

This is a standalone file, it only needs a C++11 compiler

```bash
g++ clx-rm-groups.cpp -std=c++11 -o clx-rm-groups
```

## Usage

./clx-rm-groups <path to clx> <space separated groups to remove>

Example:

```bash
./clx-rm-groups maged.clx 1 2 3 4 5 6 7
```
