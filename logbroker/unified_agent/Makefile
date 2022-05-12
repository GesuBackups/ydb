# dirs
IS_SVN := $(shell svn info 2>/dev/null 1>&2; echo $$?)

ifeq ($(IS_SVN), 0)
	ARCADIA_DIR := $(shell svn info --show-item wc-root)
else
	ARCADIA_DIR := $(shell arc root)
endif


UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
	SED_INPLACE=sed -i ''
else ifeq ($(UNAME), Linux)
	SED_INPLACE=sed -i
endif

TARGET_DIR := target
IDEA_DIR := $(or ${UNIFIED_AGENT_IDEA_DIR},${UNIFIED_AGENT_IDEA_DIR},${HOME}/.unified_agent/idea)
CLION_DIR := $(or ${UNIFIED_AGENT_CLION_DIR},${UNIFIED_AGENT_CLION_DIR},${HOME}/.unified_agent/clion)
VSCODE_CPP_DIR := $(or ${UNIFIED_AGENT_VSCODE_CPP_DIR},${UNIFIED_AGENT_VSCODE_CPP_DIR},${HOME}/.unified_agent/vscode/cpp)
GOLAND_DIR := $(or ${UNIFIED_AGENT_GOLAND_DIR},${UNIFIED_AGENT_GOLAND_DIR},${HOME}/.unified_agent/goland)
IDEA_PRJ_FILES_PRFX := $(shell basename $(IDEA_DIR))

# tools
YA := $(ARCADIA_DIR)/ya
JAVA ?= $(YA) tool java15

# Example of adding extra targets:
# $ UNIFIED_AGENT_EXTRA_CPP_TARGETS="../junk/ivanzhukov/a ../junk/ivanzhukov/b" make ide-clion

CPP_TYPES := PROGRAM|UNITTEST_FOR|GTEST|DLL|FUZZ|G_BENCHMARK|Y_BENCHMARK
CPP_TARGETS := $(UNIFIED_AGENT_EXTRA_CPP_TARGETS) .

JAVA_TARGETS := .
GO_TARGETS := .

help:
	printf "Usage:  make <target>\n\n\
    \rAvailable targets:\n\
    ide-idea               - create an IntelliJ IDEA project\n\n\
    ide-idea-clean         - remove directory with an IntelliJ IDEA project\n\n\
    re-ide-idea            - recreate an IntelliJ IDEA project (CAUTION! it will drop\n\
                             $(IDEA_DIR) folder and all files in it)\n\n\
    ide-clion              - create a CLion ide project\n\n\
    ide-clion-clean        - remove directory with a CLion ide project\n\n\
    re-ide-clion           - recreate a Clion ide project (CAUTION! it will drop\n\
                             $(CLION_DIR) folder and all files in it)\n\n\
    ide-vscode-cpp         - create a VSCode cpp ide project\n\n\
    ide-vscode-cpp-clean   - remove directory with a VSCode cpp ide project\n\n\
    ide-goland             - create an Goland project\n\n\
    ide-goland-clean       - remove directory with a Goland ide project\n\n\
    re-ide-goland          - recreate a Goland ide project (CAUTION! it will drop\n\
					         $(GOLAND_DIR) folder and all files in it)\n\n\
    protos-idea            - force protobufs update in IDEA\n\n\
    protos-clion           - force protobufs update in Clion\n\n\
    protos-goland          - force protobufs update in Goland\n\n\
    style-java             - run java codestyle analyzer\n\n\
    style-cpp              - run cpp codestyle analyzer\n\n\
    build-java             - build java targets\n\n\
    build-cpp              - build cpp targets\n\n\
    build-go               - build go targets\n\n\
    help                   - print this help message. If you forget target name then\n\
                             write 'make ' and hit <TAB> button twice. To see all\n\
                             executed commands add VERBOSE=1 in front of make:\n\
                             $$ VERBOSE=1 make build\n\n\n\
    \rYou can override default directory where IDE projects will be generated.\n\
	\rUse following environment variables:\n\
    * UNIFIED_AGENT_IDEA_DIR - for IntelliJ IDEA project direcotry\n\
    * UNIFIED_AGENT_CLION_DIR - for CLion project direcotry\n\
    * UNIFIED_AGENT_GOLAND_DIR - for Goland project direcotry\n\
    \r"

$(IDEA_DIR):
	echo "Create directory $@"
	mkdir -p $@

$(CLION_DIR):
	echo "Create directory $@"
	mkdir -p $@

$(GOLAND_DIR):
	echo "Create directory $@"
	mkdir -p $@

ide-idea: $(IDEA_DIR)
	echo "Checkout dependencies"
	# Hack to fix DEVTOOLSSUPPORT-6897
	# mkdir -p $(IDEA_DIR)/solomon/protos $(IDEA_DIR)/solomon/docker $(IDEA_DIR)/solomon/packages $(IDEA_DIR)/solomon/graphite/configs $(IDEA_DIR)/solomon/configs
	$(YA) tool ag -w 'JAVA_PROGRAM|JTEST|JAVA_LIBRARY' -l -G ya.make $(JAVA_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) make --checkout -j0 -DJDK_VERSION=15
	echo "Create IntelliJ IDEA project"
	$(YA) tool ag -w 'JAVA_PROGRAM|JTEST|JAVA_LIBRARY' -l -G ya.make $(JAVA_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) ide idea -DJDK_VERSION=15 \
			-DJDK_LANGUAGE_LEVEL=15_PREVIEW \
			-DUSE_PREBUILT_TOOLS \
			--host-platform-flag=USE_PREBUILT_TOOLS \
			--project-root=$(IDEA_DIR) \
			--directory-based \
			--group-modules=flat \
			--iml-in-project-root \
			--auto-exclude-symlinks \
			--external-content-root-module protos \
			--external-content-root-module configs \
			--external-content-root-module graphite/configs \
			--external-content-root-module docker \
			--external-content-root-module packages \
			--external-content-root-module scripts
	find $(IDEA_DIR) -name '*.iml' -type 'f' | xargs $(SED_INPLACE) 's/\"JDK_15\"/\"JDK_15_PREVIEW\"/g'
	echo "Now you can open project $(IDEA_DIR)/"

ide-idea-clean:
	echo "Drop IntelliJ IDEA project"
	$(RM) -r $(IDEA_DIR)

re-ide-idea: ide-idea-clean ide-idea
	which idea > /dev/null && idea $(IDEA_DIR)/unified_agent.ipr

protos-idea:
	find $(IDEA_DIR)/ -name '*proto*.jar' -exec touch {} \;

protos-clion:
	$(YA) tool ag -w '$(CPP_TYPES)' -l -G ya.make $(CPP_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) make --host-platform-flag=USE_PREBUILT_TOOLS \
			--add-result .pb.h \
			--add-result .pb.cc \
			--keep-going \
			--replace-result

protos-goland:
	$(YA) tool ag -w 'GO_PROGRAM|GO_TEST_FOR|GO_LIBRARY' -l -G ya.make $(GO_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) make --host-platform-flag=USE_PREBUILT_TOOLS \
			--add-result .go \
			--replace-result \
			--keep-going \
			--replace-result

style-java:
	$(YA) make -tA --style \
		-DUSE_PREBUILT_TOOLS \
		-DJDK_VERSION=15 \
		--host-platform-flag=USE_PREBUILT_TOOLS \
		--profile-test-tool JAVA_STYLE $(JAVA_TARGETS)

style-cpp:
	$(YA) clang-tidy

build-java:
	$(YA) tool ag -w JAVA_PROGRAM -l -G ya.make $(JAVA_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) make -DJDK_VERSION=15 -DUSE_PREBUILT_TOOLS --host-platform-flag=USE_PREBUILT_TOOLS

build-cpp:
	$(YA) tool ag -w 'PROGRAM|GTEST' -l -G ya.make $(CPP_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) make

ide-clion:
	echo "Checkout dependencies"
	$(YA) make --checkout -j0 $(CPP_TARGETS)
	echo "Create CLion project for targets:"
	echo $(CPP_TARGETS) | tr ' ' '\n' | cat -n
	$(YA) tool ag -w '$(CPP_TYPES)' -l -G ya.make $(CPP_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) ide clion -T=UnifiedAgentCPP -P=$(CLION_DIR) --make-args="--no-src-links"
	echo "Now you can open project $(CLION_DIR)/CMakeLists.txt"

ide-clion-clean:
	echo "Drop CLion project"
	$(RM) -r $(CLION_DIR)

re-ide-clion: ide-clion-clean ide-clion
	which clion > /dev/null && clion $(CLION_DIR)/CMakeLists.txt

ide-vscode-cpp:
	echo "Checkout dependencies"
	$(YA) make --checkout -j0 $(CPP_TARGETS)
	echo "Create a VSCode cpp project for targets:"
	echo $(CPP_TARGETS) | tr ' ' '\n' | cat -n
	$(YA) tool ag -w '$(CPP_TYPES)' -l -G ya.make $(CPP_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) ide vscode-clangd -P=$(VSCODE_CPP_DIR) --use-arcadia-root --files-visibility=all -o=$(VSCODE_CPP_DIR)/build
	echo "Now you can open project $(VSCODE_CPP_DIR)"

ide-vscode-cpp-clean:
	echo "Dropping a VSCode cpp project"
	$(RM) -r $(VSCODE_CPP_DIR)

ide-goland: protos-goland
	echo "Checkout dependencies"
	$(YA) make --checkout -j0 $(GO_TARGETS)
	echo "Create Goland project for targets:"
	echo $(GO_TARGETS) | tr ' ' '\n' | cat -n
	$(YA) tool ag -w 'GO_PROGRAM|GO_TEST_FOR|GO_LIBRARY' -l -G ya.make $(GO_TARGETS) | sed 's|/ya.make||' \
		| xargs $(YA) ide goland -P=$(GOLAND_DIR)
	echo "Now you can open project $(GOLAND_DIR)"

ide-goland-clean:
	echo "Drop Goland project"
	$(RM) -r $(GOLAND_DIR)

re-ide-goland: ide-goland-clean ide-goland
	which goland > /dev/null && goland $(GOLAND_DIR)

.PHONY: ide-idea ide-idea-clean re-ide-idea protos-idea style-java build-java
.PHONY: ide-clion ide-clion-clean re-ide-clion protos-clion
.PHONY: ide-vscode-app ide-vscode-cpp-clean
.PHONY: ide-goland ide-goland-clean re-ide-goland protos-goland

ifndef VERBOSE
.SILENT:
endif
