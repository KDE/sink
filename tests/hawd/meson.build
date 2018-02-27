project('hawd', 'c', 'cpp',
        default_options : ['c_std=c11', 'cpp_std=c++11'])

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: ['Core', 'Gui'])

lib_srcs = ['datasetdefinition.cpp']
libhawd_lib = shared_library('libhawd', lib_srcs,
    dependencies: qt5_dep)

