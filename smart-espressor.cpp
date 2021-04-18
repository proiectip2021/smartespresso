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
#include <csignal>
#include <string>
#include <cmath>
#include <ctime>

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
            : httpEndpoint(std::make_shared<Http::Endpoint>(addr)) {}

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
    void stop() {
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;

        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called.
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&EspressorEndpoint::doAuth, this));
//        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&EspressorEndpoint::setSetting, this));
//        Routes::Get(router, "/settings/:settingName/", Routes::bind(&EspressorEndpoint::getSetting, this));

        // Get details about espressor quantities or one explicit coffee
        Routes::Get(router, "/details", Routes::bind(&EspressorEndpoint::getDetails,
                                                     this));

        Routes::Get(router, "/details/stats",
                    Routes::bind(&EspressorEndpoint::getNumberOfCoffees, this));     // Number of coffees made today


        Routes::Get(router, "/boilWater", Routes::bind(&EspressorEndpoint::boilWater, this)); //Boil water

        Routes::Get(router, "/refill", Routes::bind(&EspressorEndpoint::refill, this)); // Refill



        // Prepare and choose your coffee route
        Routes::Post(router, "/options/:name", Routes::bind(&EspressorEndpoint::chooseCoffee, this));
        Routes::Post(router, "/options/:name/:water/:milk/:coffee",
                     Routes::bind(&EspressorEndpoint::chooseCoffee, this));
    }

    // One common route function for details about both espressor and explicit coffee
    void getDetails(const Rest::Request &request, Http::ResponseWriter response) {
        string about = "";
        string coffeeName = "";
        string property = "";

        // Check if there is a json object in the request body
        // else => return "No_Content" response
        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());

            // check if we have have the "about" key in the json
            // else => return "Bad_Request" response
            if (reqBody.contains("about")) {
                about = reqBody.value("about", "nothing");

                // case1: about == "espressor"
                if (about == "espressor") {

                    // check the json structure
                    // if there is something different from mockup model => "Bad_Request" response
                    if ((reqBody.size() == 2 && !reqBody.contains("property")) || reqBody.size() > 2) {
                        response.send(Http::Code::Bad_Request,
                                      "Your request contains unnecessary properties. Check again!");
                    }

                    // get the property value
                    // if it's missing => it will take the default value "nothing" => all the espressor properties
                    // will be returned
                    property = reqBody.value("property", "nothing");

                    // getting the espressor details
                    std::vector<std::string> espressorDetails = esp.getEspressor(property);

                    // no property found with that name => "Not_Found" response
                    if (espressorDetails[0] == "property_not_found") {
                        response.send(Http::Code::Not_Found,
                                      "Our espressor does not have a '" + property + "' property.");
                    }
                        // everything is ok! => return json response with the requested details
                    else {
                        Guard guard(EspressorLock);
                        // call decrease function and check/notice if there's any/not much water/milk/coffee left

                        // In this response I also add a couple of headers, describing the server that sent this
                        // response, and the way the content is formatted.
                        using namespace Http;
                        response.headers()
                                .add<Header::Server>("pistache/0.1")
                                .add<Header::ContentType>(MIME(Application, Json));

                        json e = {};

                        if (espressorDetails.size() == 1) {
                            e = {
                                    {"1",      "Espressor's " + property + ":"},
                                    {property, espressorDetails[0]}
                            };
                        } else {
                            e = {
                                    {"1",                  "Espressor current quantities:"},
                                    {"water",              espressorDetails[0]},
                                    {"milk",               espressorDetails[1]},
                                    {"coffee",             espressorDetails[2]},
                                    {"filters_usage_rate", espressorDetails[3]},
                                    {"coffees_made_today", espressorDetails[4]}
                            };
                        }

                        std::string s = e.dump();
                        response.send(Http::Code::Ok, s);
                    }
                    // case2: about == "coffee"
                } else if (about == "coffee") {

                    // check the json structure
                    // if there is something different from mockup model => "Bad_Request" response
                    if ((reqBody.size() == 3 && !reqBody.contains("property")) || reqBody.size() > 3) {
                        response.send(Http::Code::Bad_Request,
                                      "Your request contains unnecessary properties. Check again!");
                    }

                    // make sure that we have the type of coffee
                    if (reqBody.contains("coffeeName")) {
                        coffeeName = reqBody.value("coffeeName", "nothing");
                    }
                        // if not => "No_Content" response
                    else {
                        response.send(Http::Code::No_Content,
                                      "You must choose one type of coffee you want details about!");
                    }

                    // get the property value
                    // if it's missing => it will take the default value "nothing" => all the needed quantities
                    // will be returned
                    property = reqBody.value("property", "nothing");
                    std::vector<std::string> coffeeDetails = esp.getCoffee(coffeeName, property);

                    // no coffee type with that name => "Not_Found" response
                    if (coffeeDetails[0] == "coffee_not_found") {
                        response.send(Http::Code::Not_Found, "Our espressor can not make this type of coffee...");
                    }
                        // no coffee property with that name => "Not_Found" response
                    else if (coffeeDetails[0] == "property_not_found") {
                        response.send(Http::Code::Not_Found,
                                      "The type of coffee that you requested does not have this property...");
                    }
                        // everything is ok! => json response with coffe details
                    else {
                        Guard guard(EspressorLock);
                        // call decrease function and check/notice if there's any/not much water/milk/coffee left

                        // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
                        using namespace Http;
                        response.headers()
                                .add<Header::Server>("pistache/0.1")
                                .add<Header::ContentType>(MIME(Application, Json));

                        json c = {};

                        if (coffeeDetails.size() == 1) {
                            c = {
                                    {"1",      "The quantity of " + property + " needed for " + coffeeName + " is:"},
                                    {property, coffeeDetails[0]}
                            };
                        } else {
                            c = {
                                    {"1",      "The quantities needed for " + coffeeName + " are:"},
                                    {"water",  coffeeDetails[0]},
                                    {"milk",   coffeeDetails[1]},
                                    {"coffee", coffeeDetails[2]},
                                    {"time",   coffeeDetails[3]}
                            };
                        }

                        std::string s = c.dump();
                        response.send(Http::Code::Ok, s);
                    }

                } else {
                    response.send(Http::Code::No_Content,
                                  "No details available about " + about + ". Choose between: espressor/coffee!");
                }
            } else {
                response.send(Http::Code::Bad_Request,
                              "You must choose if you want details about the espressor current quantities or about one specific type of coffee!");
            }
        } else {
            response.send(Http::Code::No_Content,
                          "You must choose if you want details about the espressor current quantities or about one specific type of coffee!");
        }
    }


    // Get current espressor quantities function
    void getNumberOfCoffees(const Rest::Request &request, Http::ResponseWriter response) {
        string property = "nothing";

        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());
            property = reqBody.value("property", "nothing");
        }

        std::vector<std::string> coffeeCount = esp.getEspressor(property);

        if (coffeeCount[0] == "property_not_found") {
            response.send(Http::Code::Not_Found, "Our espressor does not have a '" + property + "' property.");
        } else {
            Guard guard(EspressorLock);
            // call decrease function and check/notice if there's any/not much water/milk/coffee left

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Application, Json));

            json e = {};

            if (coffeeCount.size() == 1) {
                e = {
                        {"1",      "Espressor's " + property + ":"},
                        {property, coffeeCount[0]}
                };
            } else {
                e = {
                        {"Number coffees made today", coffeeCount[4]}
                };
            }

            std::string s = e.dump();
            response.send(Http::Code::Ok, s);
        }
    }


    void boilWater(const Rest::Request &request, Http::ResponseWriter response) {
        int amountWater = 0;


        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());


            if (reqBody.contains("amountWater")) {
                amountWater = reqBody.value("amountWater", 0);


                if (amountWater != 0) {

                    if (reqBody.size() > 1) {
                        response.send(Http::Code::Bad_Request,
                                      "Your request contains unnecessary properties. Check again!");
                    }


                    std::vector<std::string> waterDetails = esp.getBoilWater(amountWater);

                    Guard guard(EspressorLock);

                    using namespace Http;
                    response.headers()
                            .add<Header::Server>("pistache/0.1")
                            .add<Header::ContentType>(MIME(Application, Json));

                    json e = {};

                    e = {
                            {"1", "The water will be ready in  " + waterDetails[0] + " minutes!"}
                    };

                    std::string s = e.dump();
                    response.send(Http::Code::Ok, s);


                } else {
                    response.send(Http::Code::No_Content, "No details available. Enter a quantity of water!");
                }
            } else {
                response.send(Http::Code::Bad_Request, "You must enter the amount of water! (amountWater)");
            }
        } else {
            response.send(Http::Code::No_Content, "You must enter the amount of water! (amountWater) ");
        }
    }

    void refill(const Rest::Request &request, Http::ResponseWriter response) {
        string refill = "all";

        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());


            if (reqBody.size() > 2) {
                response.send(Http::Code::Bad_Request, "Your request contains unnecessary properties. Check again!");
            }
            if (reqBody.contains("refill")) {
                refill = reqBody.value("refill", "all");

                esp.setMakeRefill(refill);

                std::vector<std::string> refillDetails = esp.makeRefill(refill);

                if (refillDetails[0] == "property_not_found") {
                    response.send(Http::Code::Not_Found, "Our espressor does not have a '" + refill + "' property.");
                }

                else {
                    Guard guard(EspressorLock);

                    using namespace Http;
                    response.headers()
                            .add<Header::Server>("pistache/0.1")
                            .add<Header::ContentType>(MIME(Application, Json));

                    json e = {};

                    if (refillDetails.size() == 1) {
                        e = {
                                {"1",    "Espressor's " + refill + " after refill:"},
                                {refill, refillDetails[0]}
                        };
                    } else {
                        e = {
                                {"1",                  "Espressor quantities after refill:"},
                                {"water",              refillDetails[0]},
                                {"milk",               refillDetails[1]},
                                {"coffee",             refillDetails[2]},
                                {"filters_usage_rate", refillDetails[3]}
                        };
                    }

                    std::string s = e.dump();
                    response.send(Http::Code::Ok, s);
                }



            } else {
                response.send(Http::Code::Bad_Request,
                              "You must enter the property for which you want to refill, or 'all' if you want to refill all properties");
            }
        } else {
            response.send(Http::Code::No_Content,
                          "'refill' field is required");
        }
    }


    void chooseCoffee(const Rest::Request& request, Http::ResponseWriter response) {
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
                    .add<Header::ContentType>(MIME(Application, Json));

            // add in json how much time it takes
            // if there's any/not much water/milk/coffee left add in json
            // if there is enough milk, water, coffee for my order then send it else send an error response
            json j = {
                    {"milk", chosenCoffee[0]},
                    {"water", chosenCoffee[1]},
                    {"coffee", chosenCoffee[2]}
            };
            std::string s = j.dump();
            time_t now = time(0);
            esp.count_coffee(now);
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


    // Defining the class of the Espressor. It should model the entire configuration of the Espressor
    class Espressor {
    public:
        explicit Espressor() = default;


        // To fix out format of string!!! ONLY 2 DECIMALS!!!
        double roundOf(double value) {
            return round(value * 100) / 100;
        }

        // Getter for espressor details
        std::vector<std::string> getEspressor(string propertyName = "nothing") {
            std::vector<std::string> response;
            double water = 0;
            double milk = 0;
            double coffee = 0;
            int filters = 0;
            int coffees = 0;

            if (propertyName == "nothing") {
                water = espressor_details.current_water.quant;
                milk = espressor_details.current_milk.quant;
                coffee = espressor_details.current_coffee.quant;
                filters = round(espressor_details.filters_usage.quant);
                coffees = round(espressor_details.coffees_today.number_today);

                cout << water << " " << milk << " " << coffee << " " << filters << " " << coffees;

                response.emplace_back(std::to_string(water));
                response.emplace_back(std::to_string(milk));
                response.emplace_back(std::to_string(coffee));
                response.emplace_back(std::to_string(filters));
                response.emplace_back(std::to_string(coffees));
            } else {
                if (propertyName == "water") {
                    water = espressor_details.current_water.quant;
                    response.emplace_back(std::to_string(water));
                } else if (propertyName == "milk") {
                    milk = espressor_details.current_milk.quant;
                    response.emplace_back(std::to_string(milk));
                } else if (propertyName == "coffee") {
                    coffee = espressor_details.current_coffee.quant;
                    response.emplace_back(std::to_string(coffee));
                } else if (propertyName == "filters_usage") {
                    filters = round(espressor_details.filters_usage.quant);
                    response.emplace_back(std::to_string(filters));
                } else if (propertyName == "coffees_today") {
                    coffees = round(espressor_details.coffees_today.number_today);
                    response.emplace_back(std::to_string(coffees));
                } else {
                    response.emplace_back("property_not_found");
                }
            }

            return response;
        }

        std::vector<std::string> makeRefill(string refill = "all") {

            std::vector<std::string> response;
            double water = 0;
            double milk = 0;
            double coffee = 0;
            int filters = 0;


            if (refill == "all") {
                water = espressor_details.current_water.quant;
                milk = espressor_details.current_milk.quant;
                coffee = espressor_details.current_coffee.quant;
                filters = espressor_details.filters_usage.quant;


                response.emplace_back(std::to_string(water));
                response.emplace_back(std::to_string(milk));
                response.emplace_back(std::to_string(coffee));
                response.emplace_back(std::to_string(filters));

            } else {
                if (refill == "water") {
                    water = espressor_details.current_water.quant;
                    response.emplace_back(std::to_string(water));

                } else if (refill == "milk") {
                    milk = espressor_details.current_milk.quant;
                    response.emplace_back(std::to_string(milk));
                } else if (refill == "coffee") {
                    coffee = espressor_details.current_coffee.quant;
                    response.emplace_back(std::to_string(coffee));
                } else if (refill == "filters_usage") {
                    filters = round(espressor_details.filters_usage.quant);
                    response.emplace_back(std::to_string(filters));
                } else {
                    response.emplace_back("property_not_found");
                }
            }

            return response;
        }

        void setMakeRefill(string refill = "all") {

            if (refill == "all") {
                espressor_details.current_water.quant = initial_values.current_water.quant;
                espressor_details.current_milk.quant = initial_values.current_milk.quant;
                espressor_details.current_coffee.quant = initial_values.current_coffee.quant;
                espressor_details.filters_usage.quant = initial_values.filters_usage.quant;

            } else {
                if (refill == "water") {
                    espressor_details.current_water.quant = initial_values.current_water.quant;
                } else if (refill == "milk") {
                    espressor_details.current_milk.quant = initial_values.current_milk.quant;

                } else if (refill == "coffee") {
                    espressor_details.current_coffee.quant = initial_values.current_coffee.quant;

                } else if (refill == "filters_usage") {
                    espressor_details.filters_usage.quant = initial_values.filters_usage.quant;
                }
            }

        }

        std::vector<std::string> getBoilWater(int amountWater) {
            std::vector<std::string> response;
            int time = 0;
            int unit = 2;
            // Atentie !!! trebuie scazuta cantitatea de apa
            if(amountWater % 100 == 0) {
                time = unit * (amountWater/100);
            } else {
                time = unit * (amountWater/100) + unit;
            }

            response.emplace_back(std::to_string(time));
            return response;
        }


        // Getter for details of one explicit coffee
        std::vector<std::string> getCoffee(string coffeeName, string propertyName = "nothing") {
            std::vector<std::string> response;
            double water = 0;
            double milk = 0;
            double coffee = 0;
            double time = 0;

            int coffeeFound = 1;
            int hasProperty = 0;

            if (propertyName != "nothing") {
                hasProperty = 1;
            }

            if (coffeeName == "black_coffee") {
                water = possible_choices.black_coffee.water.quant;
                milk = possible_choices.black_coffee.milk.quant;
                coffee = possible_choices.black_coffee.coffee.quant;
                time = possible_choices.black_coffee.time_needed;
            } else if (coffeeName == "espresso") {
                water = possible_choices.espresso.water.quant;
                milk = possible_choices.espresso.milk.quant;
                coffee = possible_choices.espresso.coffee.quant;
                time = possible_choices.espresso.time_needed;
            } else if (coffeeName == "cappuccino") {
                water = possible_choices.cappuccino.water.quant;
                milk = possible_choices.cappuccino.milk.quant;
                coffee = possible_choices.cappuccino.coffee.quant;
                time = possible_choices.cappuccino.time_needed;
            } else if (coffeeName == "flat_white") {
                water = possible_choices.flat_white.water.quant;
                milk = possible_choices.flat_white.milk.quant;
                coffee = possible_choices.flat_white.coffee.quant;
                time = possible_choices.flat_white.time_needed;
            } else {
                coffeeFound = 0;
                response.emplace_back("coffee_not_found");
            }

            if (coffeeFound == 1) {
                if (hasProperty == 1) {
                    if (propertyName == "water") {
                        response.emplace_back(std::to_string(water));
                    } else if (propertyName == "milk") {
                        response.emplace_back(std::to_string(milk));
                    } else if (propertyName == "coffee") {
                        response.emplace_back(std::to_string(coffee));
                    } else if (propertyName == "time") {
                        response.emplace_back(std::to_string(time));
                    } else {
                        response.emplace_back("property_not_found");
                    }
                } else {
                    response.emplace_back(std::to_string(water));
                    response.emplace_back(std::to_string(milk));
                    response.emplace_back(std::to_string(coffee));
                    response.emplace_back(std::to_string(time));
                }
            }

            return response;
        }

        void count_coffee(time_t day){
//            time_t t = time(NULL);
            tm* timePtr = localtime(&day);
            int new_month = timePtr->tm_mon;
            int new_day = timePtr->tm_mday;
            int new_year = timePtr->tm_year+ 1900;
            time_t old_day = espressor_details.coffees_today.day;
            int nr_coffees = espressor_details.coffees_today.number_today;
            tm* currentTime = localtime(&old_day);
            int current_month = currentTime->tm_mon;
            int current_day = currentTime->tm_mday;
            int current_year = currentTime->tm_year+ 1900;
            if (current_year!=new_year){
                espressor_details.coffees_today.number_today = 1;
                espressor_details.coffees_today.day = day;
            }
            else if(current_year==new_year && current_month!=new_month)
            {
                espressor_details.coffees_today.number_today = 1;
                espressor_details.coffees_today.day = day;
            } else if (current_day!=new_day && current_month==new_month && current_year==new_year)
            {
                espressor_details.coffees_today.number_today = 1;
                espressor_details.coffees_today.day = day;
            }
            else if (current_day==new_day && current_month==new_month && current_year==new_year)
            {
                espressor_details.coffees_today.number_today += 1;
            }

        }

        // Getter for chosenCoffee route
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

        struct coffee_counter {
            int number_today;
            time_t day={time(0)};
        };

        struct details {
            quantity filters_usage = { 3 }; //number of filters
            quantity current_milk = { 200 }; // in mL
            quantity current_water = {1800};  // in mL
            quantity current_coffee = { 300 }; // in g
            coffee_counter coffees_today = {0, time(0)}; // per day
        } espressor_details, initial_values;

        struct choices {
            coffee black_coffee = {0, 50, 20, 5};
            coffee espresso = {0, 40, 16, 7};
            coffee cappuccino = {20, 30, 20, 8};
            coffee flat_white = {40, 20, 16, 10};
            coffee your_choice = {0, 0, 0, 0};
        } possible_choices;



//        struct boolSetting {
//            string name;
//            int value;
//        } defrost;
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