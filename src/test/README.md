# Testing

## Unit tests and coverage report
```sh
sudo out/build/viu/src/test/viu-gtest
llvm-profdata merge -sparse default.profraw -o viu.profdata
```

For line by line report in text format:
```sh
llvm-cov show \
    ./out/build/viu/src/test/viu-gtest \
    -ignore-filename-regex='(external/.*)|(out/install/.*)|(src/test/.*)|(src/.*_test\.cpp)' \
    -instr-profile=viu.profdata
```

For line by line report in html format:
```sh
llvm-cov show -format=html \
    -ignore-filename-regex='(external/.*)|(out/install/.*)|(src/test/.*)|(src/.*_test\.cpp)' \
    ./out/build/viu/src/test/viu-gtest \
    -instr-profile=viu.profdata > coverage.html
```

```sh
llvm-cov report \
  -ignore-filename-regex='(external/.*)|(out/install/.*)|(src/test/.*)|(src/.*_test\.cpp)' \
  ./out/build/viu/src/test/viu-gtest \
  -instr-profile=viu.profdata
```

## Profiling
```sh
sudo valgrind \
    --tool=callgrind --dump-instr=yes -v --instr-atstart=no \
    ./cli
```
In another terminal:
* To start instrumentation:
```sh
sudo callgrind_control -i on
```
* To stop instrumentation:
```sh
sudo callgrind_control -i off
```

To view the report
```sh
kcachegrind callgrind.out.*


```
## Validate USB descriptors json
```sh
validate-json <usb descriptors json> src/json/schema/root.schema.json --verbose
```
