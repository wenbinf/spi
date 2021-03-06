import logging
import os.path
import subprocess

#
# Please modify these accordingly
#
ELF_INC_DIR = "/p/paradyn/packages/libelf/include"
ELF_LIB_DIR = "/p/paradyn/packages/libelf/lib"
DWARF_INC_DIR = "/p/paradyn/packages/libdwarf/include"
DWARF_LIB_DIR = "/p/paradyn/packages/libdwarf/lib"
XML2_INC_DIR = "/p/paradyn/packages/libxml2/include/libxml2"
XML2_LIB_DIR = "/p/paradyn/packages/libxml2/lib"

def _run_cmd(func):
    """
    Decorator to run each command
    """
    try:
        def new(*args):
            return func(*args)
        return new
    except subprocess.CalledProcessError as err:
        logging.error("%s, return code %d", err.cmd, err.returncode)
        exit(2)

@_run_cmd
def _mkdir(dir_path):
    subprocess.check_call(["mkdir", "-p", dir_path], \
                              stderr=subprocess.PIPE)
    logging.info("mkdir -p %s", dir_path)

@_run_cmd
def _git_clone(path, trg_path):
    if os.path.exists(trg_path):
        logging.info('dyninst scripts exist, skip')
    else:
        subprocess.check_call(["git", "clone", path, trg_path], \
                                  stderr=subprocess.PIPE)
        logging.info("git clone %s", path)

@_run_cmd
def _chdir(path):
    subprocess.check_call(["chdir", path], \
                              stderr=subprocess.PIPE)
    logging.info("chdir %s", path)

@_run_cmd
def _config_dyninst():
    """
    Assume current working directory is dyninst/dyninst
    """
    if os.getenv('PLATFORM') == None:
        return

    if os.path.exists("%s/make.config.local"%os.getenv('PLATFORM')): 
        return

    subprocess.check_call([\
            "./configure", \
                "--with-libelf-incdir=%s" % ELF_INC_DIR, \
                "--with-libelf-libdir=%s" % ELF_LIB_DIR, \
                "--with-libdwarf-incdir=%s" % DWARF_INC_DIR, \
                "--with-libdwarf-libdir=%s" % DWARF_LIB_DIR, \
                "--with-libxml2-libdir=%s" % XML2_INC_DIR, \
                "--with-libxml2-libdir=%s" % XML2_LIB_DIR, \
                "--prefix=%s/../%s" % (os.getcwd(), os.getenv('PLATFORM'))
            ])
    logging.info("./configure in dyninst")

@_run_cmd
def _make_dyninst():
    subprocess.check_call(["make", "install","DONT_BUILD_NEWTESTSUITE=1", "-j2"])
    logging.info("make dyninst")

@_run_cmd
def _config_spi():
    """
    Write default configuration file here:
      DYNINST_DIR=/home/wenbin/devel/dyninst/dyninst
      SP_DIR=/home/wenbin/devel/spi
      PLATFORM=x86_64-unknown-linux2.4
      DYNLINK=true
    """
    config_text = "DYNINST_DIR=%s/dyninst/dyninst\n" % os.getcwd()
    config_text += "SP_DIR=%s/spi\n" % os.getcwd()
    config_text += "PLATFORM=%s\n" % os.getenv('PLATFORM')
    config_text += "DYNLINK=true\n"
    try:
        with open('spi/config.mk', 'w') as fp:
            fp.write(config_text)
            logging.info('Write spi/config.mk')
    except EnvironmentError:
        logging.error('Failed to write spi/config.mk')
        exit(2)

@_run_cmd
def _make_spi():
    subprocess.check_call(["make", "-j2"])
    logging.info("make spi")

def build_dyninst():
    """
    Build dyninst:
    1. mkdir dyninst
    2. git clone dyninst/scripts.git
    3. git clone dyninst/dyninst.git
    4. configure dyninst
    5. make install dyninst
    """
    logging.basicConfig(level=logging.INFO)

    _mkdir('dyninst')
    _git_clone('git.dyninst.org:/pub/scripts', 'dyninst/scripts')
    _git_clone('git.dyninst.org:/pub/dyninst', 'dyninst/dyninst')
    os.chdir('dyninst/dyninst')
    _config_dyninst()
    _make_dyninst()
    os.chdir('../..')

def build_spi():
    """
    Build self-propelled instrumentation:
    1. git clone spi
    2. build config.mk file 
    3. build spi
    """
    logging.basicConfig(level=logging.INFO)

    # _git_clone('/afs/cs.wisc.edu/p/paradyn/development/wenbin/spi/spi', 'spi')
    _git_clone('git.dyninst.org:/pub/spi', 'spi')
    _config_spi()
    os.chdir('spi/%s' % os.getenv('PLATFORM'))
    _make_spi()
    os.chdir('../../')

def show_info():
    """
    We still need something to be done manually ...
    TODO (wenbin): how to make it automatic? 
    """
    pass


def main():
    build_dyninst()
    build_spi()
    show_info()

if __name__ == "__main__":
    main()
