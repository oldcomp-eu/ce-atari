# Generated by the VisualDSP++ IDDE

# Note:  Any changes made to this Makefile will be lost the next time the
# matching project file is loaded into the IDDE.  If you wish to preserve
# changes, rename this file and run it externally to the IDDE.

# The syntax of this Makefile is such that GNU Make v3.77 or higher is
# required.

# The current working directory should be the directory in which this
# Makefile resides.

# Supported targets:
#     US_init_Debug
#     US_init_Debug_clean

# Define this variable if you wish to run this Makefile on a host
# other than the host that created it and VisualDSP++ may be installed
# in a different directory.

ADI_DSP=F:\Program Files\Analog Devices\VisualDSP 5.0


# $VDSP is a gmake-friendly version of ADI_DIR

empty:=
space:= $(empty) $(empty)
VDSP_INTERMEDIATE=$(subst \,/,$(ADI_DSP))
VDSP=$(subst $(space),\$(space),$(VDSP_INTERMEDIATE))

RM=cmd /C del /F /Q

#
# Begin "US_init_Debug" configuration
#

ifeq ($(MAKECMDGOALS),US_init_Debug)

US_init_Debug : ./Debug/US_init.dxe 

./Debug/US_init.doj :./US_init.asm $(VDSP)/Blackfin/include/defBF531.h $(VDSP)/Blackfin/include/defBF532.h $(VDSP)/Blackfin/include/def_LPBlackfin.h 
	@echo ".\US_init.asm"
	$(VDSP)/easmblkfn.exe .\US_init.asm -proc ADSP-BF531 -file-attr ProjectName=US_init -g -o .\Debug\US_init.doj -MM

./Debug/US_init.dxe :./us_init.ldf ./Debug/US_init.doj 
	@echo "Linking..."
	$(VDSP)/ccblkfn.exe .\Debug\US_init.doj -T .\us_init.ldf -L .\Debug -add-debug-libpaths -flags-link -od,.\Debug -o .\Debug\US_init.dxe -proc ADSP-BF531 -flags-link -MM

endif

ifeq ($(MAKECMDGOALS),US_init_Debug_clean)

US_init_Debug_clean:
	-$(RM) ".\Debug\US_init.doj"
	-$(RM) ".\Debug\US_init.dxe"
	-$(RM) ".\Debug\*.ipa"
	-$(RM) ".\Debug\*.opa"
	-$(RM) ".\Debug\*.ti"
	-$(RM) ".\Debug\*.pgi"
	-$(RM) ".\*.rbld"

endif


