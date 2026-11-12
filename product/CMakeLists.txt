# ANDOR Analog: an ANDOR instrument built from the ANDOR Instrument Shell (shared/andor-shell),
# added by ANDOR OSP through ANDOR_EXTERNAL_PRODUCTS (the top-level CMakeLists.txt of
# arenttodal/analog). Its identifiers are its entry in ANDOR OSP's
# andor-design-system/product-registry.json; never change them once it has shipped (hosts
# recall sessions and automation by them).
include(${PROJECT_SOURCE_DIR}/shared/andor-shell/AndorInstrument.cmake)

andor_add_instrument(ANDOR_Analog
    PRODUCT_NAME "ANDOR Analog"
    SLUG analog
    PLUGIN_CODE Aanl
    BUNDLE_ID com.osp.andoranalog
    VERSION ${ANALOG_VERSION}
    SOURCES
        Source/Product.cpp
        Source/AnalogEngine.cpp
        Source/AnalogParameters.cpp
        Source/AnalogPopovers.cpp
        Source/SubPanel.cpp
        Source/ShaperDnaPanel.cpp
        Source/UserMacroState.cpp
        Source/UserMacroUI.cpp
        Source/SatellitePanel.cpp
        Source/RhythmPanel.cpp
        Source/AuxUI.cpp
        Source/ExternalInstrument.cpp
        Source/AnalogRacks.cpp
        Source/FxRouting.cpp
        Source/FxIdentity.cpp
        Source/FxParameters.cpp
        Source/FxPresets.cpp
        Source/FxRack.cpp
        Source/AnalogWellOverlays.cpp
        Source/SampleImport.cpp
        Source/SamplePatches.cpp
        Source/KontaktImport.cpp
        Source/SampleSourceUI.cpp
    # A synthesizer, not a sampler: hosts file it with the instruments that synthesize.
    VST3_CATEGORIES Instrument Synth)
target_link_libraries(ANDOR_Analog PUBLIC analog_dsp)
# EXPERIMENT (docs/experiments/KONTAKT_EXTERNAL_MORPH.md): card C hosts Kontakt's VST3. ANDOR
# Analog only; ANDOR OSP and the shell hosting nothing.
target_compile_definitions(ANDOR_Analog PUBLIC JUCE_PLUGINHOST_VST3=1)

# The walnut materials (tools/make_walnut_skin.py): rendered pieces that replace ANDOR OSP's
# of the same name in this product only (its housing's cheeks, layer A's badge).
juce_add_binary_data(analog_materials
    HEADER_NAME AnalogMaterialsData.h
    NAMESPACE AnalogMaterialsData
    SOURCES Resources/walnut-skin.zip)
target_link_libraries(ANDOR_Analog PRIVATE analog_materials)

andor_add_instrument_tests(ANDOR_Analog andor_analog_tests
    SOURCES tests/AnalogProductTests.cpp tests/AnalogSubPanelTests.cpp tests/ExternalInstrumentTests.cpp tests/AuxProductTests.cpp
    GOLDEN golden/parameter-schema.txt)

# The Kontakt experiment's stand-in (tests only, never shipped): a small VST3 instrument the
# tests host through the real VST3 path where Kontakt is not installed.
if(OSP_BUILD_TESTS AND TARGET andor_analog_tests)
    juce_add_plugin(andor_test_instrument
        PRODUCT_NAME "ANDOR Test Instrument"
        VERSION 0.1.0
        COMPANY_NAME "OSP"
        BUNDLE_ID com.osp.andortestinstrument
        PLUGIN_MANUFACTURER_CODE Ospx
        PLUGIN_CODE Atst
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument)
    target_sources(andor_test_instrument PRIVATE tests/surrogate/SurrogateInstrument.cpp)
    target_compile_definitions(andor_test_instrument PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(andor_test_instrument PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
    add_dependencies(andor_analog_tests andor_test_instrument_VST3)
    target_compile_definitions(andor_analog_tests PRIVATE
        ANDOR_TEST_INSTRUMENT_VST3="$<GENEX_EVAL:$<TARGET_PROPERTY:andor_test_instrument_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
endif()
