/*
 * Smart-Espressor made by:
 *                         Andrei Sorina-Maria
 *                         Diaconu Rebeca-Mihaela
 *                         Draghici Mircea
 *                         Nedelcu Mara-Alexandra
 *                         Versin Madalina-Ionela
 *                         Zambitchi Alexandra
 *                         GRUPA 333
 * */

#include <algorithm>
#include <signal.h>
#include <string>

#include <pistache/net.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/http_headers.h>
#include <pistache/cookie.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>
#include <pistache/common.h>
#include <valarray>
#include "json.hpp"

// for convenience
using json = nlohmann::json;

using namespace std;
using namespace Pistache;

// General advice: pay attention to the namespaces that you use in various contexts. Could prevent headaches.

// This is just a helper function to pretty-print the Cookies that one of the endpoints shall receive.
void printCookies(const Http::Request& req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const string indent(4, ' ');
    for (const auto& c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}


// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic {

    void handleReady(const Rest::Request&, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "1");
    }
}


// Definition of the EspressorEnpoint class
class EspressorEndpoint {
public:
    explicit EspressorEndpoint(Address addr)
            : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    // Initialization of the server. Additional options can be provided here
    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options()
                .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    // Server is started threaded.
    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    // When signaled server shuts down
    void stop(){
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;

        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called.
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&EspressorEndpoint::doAuth, this));
        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&EspressorEndpoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&EspressorEndpoint::getSetting, this));

        // Espressor class test route
        Routes::Get(router, "/details/:detailName/", Routes::bind(&EspressorEndpoint::getDetail, this));
        // Prepare and choose your coffee route
        Routes::Post(router, "/options/:name", Routes::bind(&EspressorEndpoint::chooseCoffee, this));
        Routes::Post(router, "/options/:name/:water/:milk/:coffee", Routes::bind(&EspressorEndpoint::chooseCoffee, this));
    }

    // Espressor class test route function
    void getDetail(const Rest::Request& request, Http::ResponseWriter response) {
        auto detailName = request.param(":detailName").as<string>();

        string* det;
        det = esp.getDetails();

        Guard guard(EspressorLock);

        string value;
        value = esp.getDetail(detailName);

        cout << value;
        if (value != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, detailName + " is " + value);
        }
        else {
            response.send(Http::Code::Not_Found, detailName + " was not found");
        }
    }

    void chooseCoffee(const Rest::Request& request, Http::ResponseWriter response){
        auto name = request.param(":name").as<string>();

        int water = 0;
        if (request.hasParam(":water")) {
            auto waterValue = request.param(":water");
            water = waterValue.as<double>();
        } else if (name == "your_choice") {
            response.send(Http::Code::No_Content, "You need to introduce water, milk and coffee for this option. If you don't want one of these just introduce 0.");
        }

        int milk = 0;
        if (request.hasParam(":milk")) {
            auto milkValue = request.param(":milk");
            milk = milkValue.as<double>();
        } else if (name == "your_choice") {
            response.send(Http::Code::No_Content, "You need to introduce water, milk and coffee for this option. If you don't want one of these just introduce 0.");
        }

        int coffee = 0;
        if (request.hasParam(":coffee")) {
            auto coffeeValue = request.param(":coffee");
            coffee = coffeeValue.as<double>();
        } else if (name == "your_choice") {
            response.send(Http::Code::No_Content, "You need to introduce water, milk and coffee for this option. If you don't want one of these just introduce 0.");
        }

        std::vector<std::string> chosenCoffee = esp.getChosenCoffee(name, milk, water, coffee);

        if (chosenCoffee[0] == "not_found") {
            response.send(Http::Code::Not_Found, "We don't have a " + name + " option in our menu.");
        } else if (chosenCoffee[0] == "bad_request") {
            response.send(Http::Code::Bad_Request, "You can't set water, milk or coffee for " + name + ".");
        } else if (chosenCoffee[0] == "empty_request") {
            response.send(Http::Code::No_Content, "Ok then! No coffee for you.");
        } else {
            Guard guard(EspressorLock);
            // call decrease function and check/notice if there's any/not much water/milk/coffee left

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Application, JsonSchemaInstance));

            // response.send(Http::Code::Ok, say how much time it takes)
            // if there's any/not much water/milk/coffee left send one more response
            // if there is enough milk, water, coffee for my order then send it
            // create an empty structure (null)
            json j = {
                {"milk", chosenCoffee[0]},
                {"water", chosenCoffee[1]},
                {"coffee", chosenCoffee[2]}
            };
            std::string s = j.dump();
            response.send(Http::Code::Ok, s);
        }
    }

    void doAuth(const Rest::Request& request, Http::ResponseWriter response) {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies()
                .add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok);
    }

    // Endpoint to configure one of the Espressor's settings.
    void setSetting(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        auto settingName = request.param(":settingName").as<string>();

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(EspressorLock);


        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<string>();
        }

        // Setting the Espressor's setting to value
        int setResponse = esp.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, settingName + " was set to " + val);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found and or '" + val + "' was not a valid value ");
        }

    }

    // Setting to get the settings value of one of the configurations of the Espressor
    void getSetting(const Rest::Request& request, Http::ResponseWriter response){
        auto settingName = request.param(":settingName").as<string>();

        Guard guard(EspressorLock);

        string valueSetting = esp.get(settingName);

        if (valueSetting != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found");
        }
    }

    // Defining the class of the Espressor. It should model the entire configuration of the Espressor
    class Espressor {
    public:
        explicit Espressor() = default;

        string* getDetails() {
            string details[5];

            details[0] = std::to_string(espressor_details.current_milk.quant);
            details[1] = std::to_string(espressor_details.current_coffee.quant);
            details[2] = std::to_string(espressor_details.current_water.quant);
            details[3] = std::to_string(espressor_details.current_coffee_filters.quant);
            details[4] = std::to_string(espressor_details.coffees_made);

            return details;
        }

        // Setting the value for one of the settings. Hardcoded for the defrosting option
        int set(string name, string value) {
            if(name == "defrost"){
                defrost.name = name;
                if(value == "true"){
                    defrost.value = true;
                    return 1;
                }
                if(value == "false"){
                    defrost.value = false;
                    return 1;
                }
            }
            return 0;
        }

        // Getter
        string get(std::string name) {
            if (name == "defrost"){
                return std::to_string(defrost.value);
            }
            else{
                return "";
            }
        }

        // Getter for test route function
        string getDetail(string name) {
            if (name == "water") {
                return std::to_string(espressor_details.current_water.quant);
            }
            else if (name == "coffee") {
                return std::to_string(espressor_details.current_coffee.quant);
            }
            else if (name == "milk") {
                return std::to_string(espressor_details.current_milk.quant);
            }
            else if (name == "numbers") {
                return std::to_string(espressor_details.coffees_made);
            }
            else if (name == "filters") {
                return std::to_string(espressor_details.current_coffee_filters.quant);
            }
            else {
                return "";
            }
        }

        std::vector<std::string> getChosenCoffee(string name, double milk, double water, double coffee) {
            std::vector<std::string> response;
            if (name != "your_choice") {
                if (milk != 0 or water != 0 or coffee != 0) {
                    response.emplace_back("bad_request");
                } else {
                    if (name == "black_coffee") {
                        response.emplace_back(std::to_string(possible_choices.black_coffee.milk.quant));
                        response.emplace_back(std::to_string(possible_choices.black_coffee.water.quant));
                        response.emplace_back(std::to_string(possible_choices.black_coffee.coffee.quant));
                    } else if (name == "espresso") {
                        response.emplace_back(std::to_string(possible_choices.espresso.milk.quant));
                        response.emplace_back(std::to_string(possible_choices.espresso.water.quant));
                        response.emplace_back(std::to_string(possible_choices.espresso.coffee.quant));
                    } else if (name == "cappuccino") {
                        response.emplace_back(std::to_string(possible_choices.cappuccino.milk.quant));
                        response.emplace_back(std::to_string(possible_choices.cappuccino.water.quant));
                        response.emplace_back(std::to_string(possible_choices.cappuccino.coffee.quant));
                    } else if (name == "flat_white") {
                        response.emplace_back(std::to_string(possible_choices.flat_white.milk.quant));
                        response.emplace_back(std::to_string(possible_choices.flat_white.water.quant));
                        response.emplace_back(std::to_string(possible_choices.flat_white.coffee.quant));
                    } else {
                        response.emplace_back("not_found");
                    }
                }
            } else {
                if (milk == 0 and water == 0 and coffee == 0) {
                    response.emplace_back("empty_request");
                } else {
                    response.emplace_back(std::to_string(milk));
                    response.emplace_back(std::to_string(water));
                    response.emplace_back(std::to_string(coffee));
                }
            }

            return response;
        }

    private:
        double boiling_water = 5; // in minutes

        struct quantity {
            double quant;
        };

        struct coffee {
            quantity milk;
            quantity water;
            quantity coffee;
            double time_needed;
        };

        struct details {
            quantity current_coffee_filters = { 3 }; //number of filters
            quantity current_milk = { 200 }; // in mL
            quantity current_water = {1800};  // in mL
            quantity current_coffee = { 300 }; // in g
            int coffees_made = 0; // per day
        } espressor_details, intial_values;

        struct choices {
            coffee black_coffee = {0, 50, 20, 5};
            coffee espresso = {0, 40, 16, 7};
            coffee cappuccino = {20, 30, 20, 8};
            coffee flat_white = {40, 20, 16, 10};
            coffee your_choice = {0, 0, 0, 0};
        } possible_choices;

        struct boolSetting {
            string name;
            int value;
        } defrost;
    };

    // Create the lock which prevents concurrent editing of the same variable
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock EspressorLock;

    // Instance of the Espressor model
    Espressor esp;

    // Defining the httpEndpoint and a router.
    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char *argv[]) {

    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0
        || sigaddset(&signals, SIGTERM) != 0
        || sigaddset(&signals, SIGINT) != 0
        || sigaddset(&signals, SIGHUP) != 0
        || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    // Instance of the class that defines what the server can do.
    EspressorEndpoint stats(addr);

    // Initialize and start the server
    stats.init(thr);
    stats.start();


    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}