# lapack

compiler = meson.get_compiler('cpp')
_dir = get_option('lapack-dir')

_libs = []

if _dir == ''

    _libs += compiler.find_library('lapack', required:true)
    _libs += compiler.find_library('blas', required:true)

else
    _libs += compiler.find_library('lapack', required:true, dirs:[_dir, _dir+'/lib'])
    _libs += compiler.find_library('blas', required:true, dirs:[_dir, _dir+'/lib'])
endif
    
deps += _libs
