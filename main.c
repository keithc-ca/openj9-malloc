/*******************************************************************************
 * Copyright (c) 2020, 2020 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include <jni.h>

typedef jint (JNICALL * CreateVM)(JavaVM **, JNIEnv **, void *);

int
main(int argc, char **argv)
{
	int jvmOptionCount = 0;
	int i = 0;
	JavaVMInitArgs vmArgs;
	JavaVMOption * options = NULL;
	JavaVM * vm = NULL;
	JNIEnv * env = NULL;
	int javaArgc = 0;
	int mainIndex = 0;
	void * handle = NULL;
	CreateVM createVM = NULL;
	jclass cls = NULL;
	jmethodID mid = NULL;
	jclass stringClass = NULL;
	jobject javaArgv = NULL;
	const char * pathtojvm = NULL;
	char * tmp = NULL;
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "No path to jvm lib specified\n");
		rc = 1;
		goto done;
	}

	pathtojvm = argv[1];

	/* Count the JVM options. */
	for (i = 2, jvmOptionCount = 0;; ++i) {
		if (i >= argc) {
			fprintf(stderr, "No class name specified\n");
			rc = 2;
			goto done;
		}
		if ('-' != argv[i][0]) {
			/* this should be the main class */
			mainIndex = i;
			javaArgc = argc - (i + 1);
			break;
		}
		if (0 == strcmp(argv[i], "-cp")) {
			i += 1; /* the classpath is next */
			if (i >= argc) {
				fprintf(stderr, "No classpath specified\n");
				rc = 2;
				goto done;
			}
		}
		jvmOptionCount += 1;
	}

	options = malloc(jvmOptionCount * sizeof(*options));
	if (NULL == options) {
		fprintf(stderr, "malloc failure: options\n");
		rc = 3;
		goto done;
	}

	memset(options, 0, jvmOptionCount * sizeof(*options));
	memset(&vmArgs, 0, sizeof(vmArgs));

	vmArgs.version = JNI_VERSION_1_8;
	vmArgs.nOptions = jvmOptionCount;
	vmArgs.options = options;
	vmArgs.ignoreUnrecognized = JNI_FALSE;

	/* Create the JVM options. */
	for (i = 2, jvmOptionCount = 0; i < mainIndex; ++i, ++jvmOptionCount) {
		char * arg = argv[i];
		if (0 == strcmp(arg, "-cp")) {
			/* convert "-cp classpath" to "-Djava.class.path=classpath" */
			const char * classpath = argv[i + 1];
			i += 1;
			tmp = malloc(strlen("-Djava.class.path=") + strlen(classpath) + 1);
			if (NULL == tmp) {
				fprintf(stderr, "malloc failure: classpath\n");
				rc = 4;
				goto done;
			}
			sprintf(tmp, "-Djava.class.path=%s", classpath);
			arg = tmp;
		}
		options[jvmOptionCount].optionString = arg;
	}

	handle = dlopen(pathtojvm, RTLD_NOW);

	if (NULL == handle) {
		fprintf(stderr, "could not open %s: %s\n", pathtojvm, dlerror());
		rc = 5;
		goto done;
	}

	createVM = (CreateVM)dlsym(handle, "JNI_CreateJavaVM");
	if (NULL == createVM) {
		fprintf(stderr, "could not lookup JNI_CreateJavaVM\n");
		rc = 6;
		goto done;
	}

	if (JNI_OK != createVM(&vm, &env, &vmArgs)) {
		fprintf(stderr, "JNI_CreateJavaVM failed\n");
		rc = 7;
		goto done;
	}

	cls = (*env)->FindClass(env, argv[mainIndex]);
	if (NULL == cls) {
		rc = 8;
		goto fail;
	}

	mid = (*env)->GetStaticMethodID(env, cls, "main", "([Ljava/lang/String;)V");
	if (NULL == mid) {
		fprintf(stderr, "cannot get main method\n");
		rc = 12;
		goto fail;
	}

	stringClass = (*env)->FindClass(env, "java/lang/String");
	if (NULL == stringClass) {
		fprintf(stderr, "cannot find String\n");
		rc = 9;
		goto fail;
	}

	javaArgv = (*env)->NewObjectArray(env, javaArgc, stringClass, NULL);
	if (NULL == javaArgv) {
		fprintf(stderr, "could not create arg array\n");
		rc = 10;
		goto fail;
	}

	/* pass the java arguments*/
	for (i = 0; i < javaArgc; ++i) {
		jstring arg = (*env)->NewStringUTF(env, argv[mainIndex + 1 + i]);
		if (NULL == arg) {
			fprintf(stderr, "could not create arg string\n");
			rc = 11;
			goto fail;
		}
		(*env)->SetObjectArrayElement(env, javaArgv, i, arg);
	}

	(*env)->CallStaticVoidMethod(env, cls, mid, javaArgv);

fail:
	(*env)->ExceptionDescribe(env);
	(*vm)->DestroyJavaVM(vm);

done:

	if (NULL != handle) {
		dlclose(handle);
	}
	if (NULL != options) {
		free(options);
	}
	if (NULL != tmp) {
		free(tmp);
	}

	return rc;
}
