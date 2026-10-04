.PHONY: host-test switch-build privacy-check
host-test:
	$(MAKE) -C host_tests test
	$(MAKE) -C tools/amazon-remote-module host-test
switch-build:
	$(MAKE) -C tools/amazon-remote-probe all
	$(MAKE) -C tools/amazon-remote-module all
privacy-check:
	python3 scripts/check_public_tree.py
