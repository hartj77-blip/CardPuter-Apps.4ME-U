# Sample IR database source

A few remotes written by hand from well-known codes, laid out like
Flipper-IRDB (`<Category>/<Brand>/<Model>.ir`), and two in its
`_Converted_/Pronto/<Letter>/<Brand>/` layout, as raw recordings of codes
the others have (apps/ir/tests/irdb-test.sh checks they are filed and
merged). CI builds the screenshots
and tests from it, and it lets the simulator show the IR Remote app without
downloading anything:

```sh
apps/ir/irdb/build_irdb.py --src apps/ir/irdb/sample --out build/irdb-sample
ZEROOS_IR_DB=$PWD/build/irdb-sample ./build/sim/zeroshell
```

The device ships the full database instead (`build_irdb.py --fetch`, see ../../README.md).
