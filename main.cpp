import std;

import viu.client;
import viu.daemon;

int main(int argc, const char* argv[])
{
    if (argc < 2 && viu::daemon::service::is_service_start()) {
        return viu::daemon::service{}.run();
    }

    return viu::client{}.run(argc, argv);
}
