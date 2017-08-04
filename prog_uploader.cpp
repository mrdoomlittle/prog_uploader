# include <boost/asio/serial_port.hpp>
# include <boost/asio.hpp>
# include <cstdio>
# include <eint_t.hpp>
# include <iostream>
# include <sys/stat.h>
# include <string.h>
# include <fcntl.h>
# include <errno.h>
# include <stdint.h>
# include <stdlib.h>
# include <unistd.h>

mdl::u8_t put_byte = 0x1;
mdl::u8_t get_byte = 0x2;
mdl::u8_t page_update = 0x3;
mdl::u8_t reset_addr = 0x4;
mdl::u8_t exec_code = 0x5;

void prog_send_instr(boost::asio::serial_port *__serial_port, mdl::u8_t __i) {
	for (;;) {
		boost::asio::write(*__serial_port, boost::asio::buffer(&__i, sizeof(mdl::u8_t)));
		if (::tcdrain(__serial_port->native_handle()) < 0) {
			fprintf(stderr, "failed to write.\n");
		}

		mdl::u8_t ack = 0;
		redo:
		try {
			boost::asio::read(*__serial_port, boost::asio::buffer(&ack, sizeof(mdl::u8_t)));
		} catch(boost::system::system_error const& e) {goto redo;}

		if (ack != 1) {
			fprintf(stderr, "sync error.\n");
		} else break;
	}
}

mdl::i8_t write_data(mdl::u8_t *__file_buff, mdl::uint_t __st_size, boost::asio::serial_port *__serial_port) {
	prog_send_instr(__serial_port, reset_addr);

	printf("writing program data.\n");
	mdl::u8_t ic = 0;
	for (mdl::u8_t *itr = __file_buff; itr != __file_buff+__st_size; itr++) {
		prog_send_instr(__serial_port, put_byte);
		if (!ic) {
			printf("prog_uploader, sent; [%x] . ", *itr);
			ic++;
		} else {
			if (ic != 64 && itr != __file_buff+__st_size-1) {
				printf("[%x] . ", *itr);
				ic++;
			} else {
				printf("[%x]\n\n", *itr);
				ic = 0;
			}
		}

		boost::asio::write(*__serial_port, boost::asio::buffer(itr, sizeof(mdl::u8_t)));
		::tcdrain(__serial_port->native_handle());
	}
	return 0;
}

mdl::i8_t validatie_data(mdl::u8_t *__file_buff, mdl::uint_t __st_size, boost::asio::serial_port *__serial_port) {
	prog_send_instr(__serial_port, reset_addr);
	printf("valadating program data.\n");
	for (mdl::u8_t *itr = __file_buff; itr != __file_buff+__st_size; itr++) {
		mdl::u8_t inbound_byte = 0;
		prog_send_instr(__serial_port, get_byte);

		redo:
		try {
			boost::asio::read(*__serial_port, boost::asio::buffer(&inbound_byte, sizeof(mdl::u8_t)));
		} catch(boost::system::system_error const& e) {goto redo;}

		if (*itr != inbound_byte) {
			fprintf(stderr, "error, byte mismatch.\n");
			fprintf(stderr, "got: %x not %x, off: %u\n", inbound_byte, *itr, itr-__file_buff);
			return -1;
		}
	}
	return 0;
}

mdl::i8_t upload_program(mdl::u8_t *__file_buff, mdl::uint_t __st_size, boost::asio::serial_port *__serial_port) {
	if (write_data(__file_buff, __st_size, __serial_port) < 0) return -1;
	prog_send_instr(__serial_port, page_update);
	if (validatie_data(__file_buff, __st_size, __serial_port) < 0) return -1;
	return 0;
}

int main(int argc, char const *argv[]) {
	if (argc < 3) {
		printf("usage: prog_uploader [binary file] [serial port].\n");
		return -1;
	}

	mdl::u8_t *file_buff = nullptr;
	char const *fpth = argv[1];
	int fd;
	if ((fd = open(fpth, O_RDONLY)) < 0) {
		fprintf(stderr, "failed to open file at: %s\n", fpth);
		return -1;
	}

	struct stat st;
	if (stat(argv[1], &st) < 0) {
		fprintf(stderr, "failed to stat file at: %s\n", fpth);
		close(fd);
		return -1;
	}

	st.st_size -= 1;

	file_buff = (mdl::u8_t*)malloc(st.st_size);
	read(fd, file_buff, st.st_size);
	printf("loaded binary file with size: %u\n", st.st_size);
	close(fd);

	boost::asio::serial_port_base::character_size chr_size(8);
	boost::asio::io_service io_service;
	boost::asio::serial_port serial_port(io_service, argv[2]);
	serial_port.set_option(boost::asio::serial_port_base::baud_rate(38400));
	serial_port.set_option(chr_size);

	printf("waiting for serial port to open, please wait...\n");
	while(!serial_port.is_open()){};
	printf("serial port is now open.\n");


	printf("please wait for 8seconds for the uploader to setup.\n");
	usleep(8000000); // wait
	printf("thanks for waiting, please input command.\n");

	char instr_buff[128];
	while(1) {
		printf("prog_uploader: ");
		scanf("%s", instr_buff);

		if (!strcmp(instr_buff, "help")) {
			printf("commands: [help, upload, exec, exit].\n");
		} else if (!strcmp(instr_buff, "upload")) {

			printf("uploader program data to device.\n");
			if (upload_program(file_buff, st.st_size, &serial_port) < 0) {
				fprintf(stderr, "upload failed.\n");
				break;
			}

			printf("program data successfully uploaded.\n");

		} else if (!strcmp(instr_buff, "exec")) {
			prog_send_instr(&serial_port, exec_code);
		} else if (!strcmp(instr_buff, "exit")) {
			printf("goodbyte.\n");
			break;
		}
	}

	err:
	serial_port.close();
	free(file_buff);
}
