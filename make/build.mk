# comment out or override if you want to see the full output of each command
NOECHO ?= @

$(OUTBIN): $(OUTELF)
	@echo generating image: $@
	$(NOECHO)$(SIZE) $<
	$(NOCOPY)$(OBJCOPY) -O binary $< $@

ifeq ($(ENABLE_TRUSTZONE), 1)
$(OUTELF): $(ALLOBJS) $(LINKER_SCRIPT) $(OUTPUT_TZ_BIN)
	@echo linking $@
	$(NOECHO)$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(OUTPUT_TZ_BIN) $(ALLOBJS) $(LIBGCC) -o $@
else
$(OUTELF): $(ALLOBJS) $(LINKER_SCRIPT)
	$(NOECHO)for obj in $(SHIMPATCHOBJS); do cp $$obj $$obj.patched.o; done
	
	$(NOECHO)if [ -n "$(SHIMOBJS)" ];then \
		echo weakening shim symbols; \
		$(OBJDUMP) -t $(SHIMOBJS) | awk '$$2 == "g"' | awk '{print $$6}' > $(BUILDDIR)/lk.weaken.syms; \
		for obj in $(SHIMPATCHOBJS); do $(OBJCOPY) --weaken-symbols=$(BUILDDIR)/lk.weaken.syms $$obj.patched.o; done; \
	fi
	
	@echo linking $@
	$(NOECHO)$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALLOBJS_PATCHED) $(LIBGCC) -o $@
endif


$(OUTELF).sym: $(OUTELF)
	@echo generating symbols: $@
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) > $@

$(OUTELF).lst: $(OUTELF)
	@echo generating listing: $@
	$(NOECHO)$(OBJDUMP) -Mreg-names-raw -d $< | $(CPPFILT) > $@

$(OUTELF).debug.lst: $(OUTELF)
	@echo generating listing: $@
	$(NOECHO)$(OBJDUMP) -Mreg-names-raw -S $< | $(CPPFILT) > $@

$(OUTELF).size: $(OUTELF)
	@echo generating size map: $@
	$(NOECHO)$(NM) -S --size-sort $< > $@

ifeq ($(ENABLE_TRUSTZONE), 1)
$(OUTPUT_TZ_BIN): $(INPUT_TZ_BIN)
	@echo generating TZ output from TZ input
	$(NOECHO)$(OBJCOPY) -I binary -B arm -O elf32-littlearm $(INPUT_TZ_BIN) $(OUTPUT_TZ_BIN)
endif

$(OUTELF_STRIP): $(OUTELF)
	@echo generating stripped elf: $@
	$(NOECHO)$(STRIP) -S $< -o $@

include arch/$(ARCH)/compile.mk

