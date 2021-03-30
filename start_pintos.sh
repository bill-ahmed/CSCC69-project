docker rm --force pintos # If it already exists
docker run --name pintos -it -v "$(pwd):/pintos" thierrysans/pintos bash -c "echo \"alias pp='cd /pintos/src/vm/build && pintos-gdb kernel.o'\" > ~/.bashrc; source ~/.bashrc; bash"
