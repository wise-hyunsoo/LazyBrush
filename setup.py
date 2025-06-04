from setuptools import setup, Extension
import numpy

setup(
    name='lazybrush',
    version='0.1.0',
    ext_modules=[
        Extension(
            'lazybrush',
            ['lazybrush.c'],
            include_dirs=[numpy.get_include()],
        )
    ]
)