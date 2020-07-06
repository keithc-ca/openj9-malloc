<!--
Copyright (c) 2020, 2020 IBM Corp. and others

This program and the accompanying materials are made available under
the terms of the Eclipse Public License 2.0 which accompanies this
distribution and is available at https://www.eclipse.org/legal/epl-2.0/
or the Apache License, Version 2.0 which accompanies this distribution and
is available at https://www.apache.org/licenses/LICENSE-2.0.

This Source Code may also be made available under the following
Secondary Licenses when the conditions for such availability set
forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
General Public License, version 2 with the GNU Classpath
Exception [1] and GNU General Public License, version 2 with the
OpenJDK Assembly Exception [2].

[1] https://www.gnu.org/software/classpath/license.html
[2] http://openjdk.java.net/legal/assembly-exception.html

SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
-->
## Overriding Basic Memory Management

This project provides a proof-of-concept for interjecting an alternate implementation of `malloc()`, `free()` and related functions.

The file `malloc.cpp` provides the new implementations of `malloc()`, `realloc()`, `calloc()` and `free()`.

The resulting object file, `malloc.o` can be linked with any C application: this project demonstrates use with Java via JNI.

## How to Build and Use

Follow these steps to build this project (adjusting `JAVA_HOME` to suit your environment).

```
git clone https://github.com/keithc-ca/openj9-malloc.git
cd openj9-malloc
export JAVA_HOME=/usr/lib/jvm/adoptojdk-java-11
make
```

The resulting executable (`launcher`) is like a simple version of the `java` executable that is part of a normal JRE.
The first argument must be the path of the `jvm` shared library of the JRE to be used.
The two commands below do the same thing (although with different basic memory allocation implementations).

```
$JAVA_HOME/bin/java -cp . Hello
./launcher $JAVA_HOME/lib/server/libjvm.so -cp . Hello
```
