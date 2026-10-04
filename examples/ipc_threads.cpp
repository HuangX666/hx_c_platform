#include <hx/Ipc.hpp>
#include <hx/Thread.hpp>
#include <cstdio>
#include <cstdlib>

struct Sender {
	hx_ipc *endpoint;
	uint64_t channel;
	hx_os_status status;
};

static void *send_messages(void *user)
{
	Sender *sender = static_cast<Sender *>(user);
	hx_ipc *borrowed = sender->endpoint;

	hx::Ipc connection(borrowed);

	for (int index = 0; index < 5; ++index) {
		char text[64];

		std::snprintf(text, sizeof(text), "device %llu, request %d",
			static_cast<unsigned long long>(sender->channel),
			index);

		auto reply = connection.requestText(text, sender->channel);
		if (!reply) {
			sender->status = reply.status();

			return NULL;
		}

		std::printf("Reply: %s\n", reply.value().text());
	}

	sender->status = HX_OS_OK;

	return NULL;
}

/* Start: hx_os_ipc_server local://demo 20
 * Then:  hx_os_ipc_threads local://demo
 * Four business threads share one connection and its single I/O thread.
 */
int main(int argc, char **argv)
{
	hx_ipc *owner = NULL;

	hx::Ipc connection(owner);

	hx_os_thread *workers[4] = {};
	Sender senders[4] = {};
	int started = 0;
	int result = 0;
	auto connected =
		connection.connect(argc > 1 ? argv[1] : "local://demo");

	if (!connected) {
		std::fprintf(stderr, "Connect: %d\n", connected.status());
		(void)connection.closeUntil(hx_os_deadline_after_ms(5000));

		return 1;
	}

	for (int index = 0; index < 4; ++index) {
		senders[index].endpoint = owner;
		senders[index].channel = static_cast<uint64_t>(index + 1);

		auto thread = hx::Thread::create(
			workers[index], send_messages, &senders[index]);
		if (!thread) {
			result = 1;
			break;
		}

		++started;
	}

	auto deadline = hx_os_deadline_after_ms(30000);

	for (int index = 0; index < started; ++index) {
		hx::Thread thread(workers[index]);
		if (!thread.closeUntil(deadline)) {
			/* Fatal demo failure: keep borrowed storage alive until
			 * process exit, never detach a worker using stack data.
			 */
			std::fputs("Worker did not stop in time.\n", stderr);
			std::exit(EXIT_FAILURE);
		}

		if (senders[index].status != HX_OS_OK)
			result = 1;
	}

	if (!connection.closeUntil(hx_os_deadline_after_ms(5000)))
		result = 1;

	if (!result)
		std::puts("20 replies matched across 4 business threads.");

	return result;
}
