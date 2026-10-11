TOOLS_IMAGE ?= kimformer-tools
INFORM ?= $(DOCKER_RUN) $(TOOLS_IMAGE) inform6
DFROTZ ?= $(DOCKER_RUN) -i $(TOOLS_IMAGE) dfrotz
INFORM_FLAGS := -v3 -e -~S '$$OMIT_UNUSED_ROUTINES=1'

.PHONY: tools-image demo-check

tools-image:
	docker build -t $(TOOLS_IMAGE) docker/

demo: $(BUILD)/demo.z3

$(BUILD)/demo.z3: games/demo.inf
	@mkdir -p $(BUILD)
	$(INFORM) $(INFORM_FLAGS) $< $@

$(BUILD)/demo.dfrotz.txt: $(BUILD)/demo.z3 games/demo.walkthrough
	rm -f $(BUILD)/demo.sav*
	$(DFROTZ) -m -p -q -Z 3 -R $(BUILD) $< < games/demo.walkthrough > $@

demo-check: $(BUILD)/demo.dfrotz.txt
	awk -f games/expect.awk games/demo.expect $<
