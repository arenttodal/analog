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
    VERSION 0.1.0
    SOURCES
        Source/Product.cpp
        Source/AnalogEngine.cpp
        Source/AnalogParameters.cpp
        Source/AnalogPopovers.cpp
        Source/AnalogOscillatorWorkbench.cpp
    # A synthesizer, not a sampler: hosts file it with the instruments that synthesize.
    VST3_CATEGORIES Instrument Synth)
target_link_libraries(ANDOR_Analog PUBLIC analog_dsp)

andor_add_instrument_tests(ANDOR_Analog andor_analog_tests
    SOURCES tests/AnalogProductTests.cpp
    GOLDEN golden/parameter-schema.txt)
