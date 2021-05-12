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
#include <typeinfo>

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

        // Details routes
        Routes::Get(router, "/details/quantities", Routes::bind(&EspressorEndpoint::getCurrentQuantities, this));        // Get details about current quantities
        Routes::Get(router, "/details/quantities/:property", Routes::bind(&EspressorEndpoint::getCurrentQuantities, this));        // Get details about current quantity of one property


        Routes::Get(router, "/details/coffee/:coffeeName", Routes::bind(&EspressorEndpoint::getCoffeeIngredients,this));        // Get details about one explicit coffee
        Routes::Get(router, "/details/coffee/:coffeeName/:property", Routes::bind(&EspressorEndpoint::getCoffeeIngredients,this));        // Get details about one explicit coffee ingredient

        Routes::Get(router, "/details/stats", Routes::bind(&EspressorEndpoint::getNumberOfCoffees, this));     // Get the number of coffees made today

        // Espressor's functionalities
        Routes::Post(router, "/boilWater", Routes::bind(&EspressorEndpoint::boilWater, this)); //Boil water

        Routes::Post(router, "/refill", Routes::bind(&EspressorEndpoint::refill, this)); // Refill espressor quantities


        // Prepare and choose your coffee route
        Routes::Post(router, "/options", Routes::bind(&EspressorEndpoint::chooseCoffee, this));

        // Prepare coffee with alarm
        Routes::Post(router, "/prepareAlarm", Routes::bind(&EspressorEndpoint::makeAlarmCoffee, this));
        Routes::Get(router, "/alarm/defaultCoffee", Routes::bind(&EspressorEndpoint::showDefaultCoffee, this));
        Routes::Post(router, "/alarm/defaultCoffee", Routes::bind(&EspressorEndpoint::setDefaultCoffee, this));
    }

    // Get details of  current quantities
    void getCurrentQuantities(const Rest::Request &request, Http::ResponseWriter response) {
        string property = "nothing";    // default value

        // check if there is a property parameter given
        // if not -> we'll return all the current quantities of espressor
        if (request.hasParam(":property")) {
            auto propertyName = request.param(":property");
            property = propertyName.as<string>();
        }

        // get the current quantities details
        // call the getter function with parameter 'route' = 0 to know what info to return
        std::vector<std::string> espressorDetails = esp.getEspressor(0, property);

        // check if we found the given property
        if (espressorDetails[0] == "property_not_found") {
            response.send(Http::Code::Not_Found, "Our espressor does not have a '" + property + "' property.");
        }
        else {
            Guard guard(EspressorLock);
            // call decrease function and check/notice if there's any/not much water/milk/coffee left

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Application, Json));

            json e = {};

            // there was a property parameter given
            if (espressorDetails.size() == 1) {
                e = {
                        {property, espressorDetails[0]}
                };
            }
                // else show all the current quantities
            else {
                e = {
                        {"water", espressorDetails[0]},
                        {"milk", espressorDetails[1]},
                        {"coffee", espressorDetails[2]},
                        {"filtersUsage", espressorDetails[3]}
                };
            }

            std::string s = e.dump();
            response.send(Http::Code::Ok, s);
        }

    }


    // Get ingredients details of one explicit coffee
    void getCoffeeIngredients(const Rest::Request &request, Http::ResponseWriter response) {
        string property = "nothing";    // default value
        string coffeeName = request.param(":coffeeName").as<string>();      // required parameter

        // check if there is a property parameter given
        // if not -> we'll return all the needed ingredients of given coffee type
        if (request.hasParam(":property")) {
            auto propertyName = request.param(":property");
            property = propertyName.as<string>();
        }

        std::vector<std::string> coffeeDetails = esp.getCoffee(coffeeName, property);

        // invalid coffee type given
        if (coffeeDetails[0] == "coffee_not_found") {
            response.send(Http::Code::Not_Found, "Our espressor can not make this type of coffee...");
        }
            // invalid property given
        else if (coffeeDetails[0] == "property_not_found") {
            response.send(Http::Code::Not_Found, "The type of coffee that you requested does not have this property...");
        } else {
            Guard guard(EspressorLock);
            // call decrease function and check/notice if there's any/not much water/milk/coffee left

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Application, Json));

            json c = {};

            // valid property parameter given
            if (coffeeDetails.size() == 1) {
                c = {
                        {property, coffeeDetails[0]}
                };
            }
                // no property -> return them all
            else {
                c = {
                        {"water", coffeeDetails[0]},
                        {"milk", coffeeDetails[1]},
                        {"coffee", coffeeDetails[2]},
                        {"time", coffeeDetails[3]}
                };
            }

            std::string s = c.dump();

            response.send(Http::Code::Ok, s);
        }
    }


    // Get current espressor quantities function
    void getNumberOfCoffees(const Rest::Request &request, Http::ResponseWriter response) {
        string property = "number of coffees today";
        //Getting the current time to see how many coffees were made today
        time_t now = time(0);
        // If the day has changed, the counter resets
        esp.reset_coffee_counter(now);
        std::vector<std::string> coffeeCount = esp.getEspressor(1, property);

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

            json e = {{property, coffeeCount[0]}};

            std::string s = e.dump();
            response.send(Http::Code::Ok, s);
        }
    }


    void boilWater(const Rest::Request &request, Http::ResponseWriter response) {
        int amountWater = 0;


        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());

            if (reqBody.size() > 1) {
                response.send(Http::Code::Bad_Request,
                              "Your request contains unnecessary properties. Check again!");
            }


            if (reqBody.contains("amountWater")) {
                for (auto& x : reqBody.items()) {
                    if (x.key() == "amountWater") {
                        if(x.value().is_number() != 1)
                            response.send(Http::Code::Bad_Request,  "Amount of water must be a number! ");
                    }
                }

                amountWater = reqBody.value("amountWater", 0);


                if (amountWater > 0) {

                    std::vector<std::string> waterDetails = esp.getBoilWaterTime(amountWater);

                    std::vector<double> verifyWater = esp.verifyQuantity("water", amountWater);

                    if( verifyWater[0] == 1) {
                        esp.setDecrease("water", verifyWater[1]);
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
                        response.send(Http::Code::Bad_Request, "Not enough water.");
                    }

                } else {
                    response.send(Http::Code::No_Content, "No details available. Enter a correct quantity of water!");
                }
            } else {
                response.send(Http::Code::Bad_Request, "You must enter the 'amountWater' field and the amount of water! (amountWater)");
            }
        } else {
            response.send(Http::Code::No_Content, "'amountWater' is required! ");
        }
    }

    void refill(const Rest::Request &request, Http::ResponseWriter response) {
        string refill = "all";

        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());


            if (reqBody.size() > 1) {
                response.send(Http::Code::Bad_Request, "Your request contains unnecessary properties. Check again!");
            }

            if (reqBody.contains("refill")) {
                for (auto& x : reqBody.items()) {
                    if (x.key() == "refill") {
                        if(x.value().is_string() != 1)
                            response.send(Http::Code::Bad_Request,  "You must enter the property for which you want to refill: water, milk, coffee, filters_usage or all!");
                    }
                }
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
                              "You must enter the 'refill' field and the property for which you want to refill, or 'all' if you want to refill all properties!");
            }
        } else {
            response.send(Http::Code::No_Content,
                          "'refill' field is required!");
        }
    }

    void chooseCoffee(const Rest::Request& request, Http::ResponseWriter response) {
        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());

            if (reqBody.size() > 4) {
                response.send(Http::Code::Bad_Request, "Your request contains unnecessary properties. Check again!");
            }

            for (auto& x : reqBody.items()) {
                if ((x.key() != "name") && (x.key() != "coffee") && (x.key() != "water") && (x.key() != "milk")) {
                    response.send(Http::Code::Bad_Request, x.key() + " is not a valid property.");
                } else {
                    if (x.key() != "name") {
                        if (x.value().is_number() != 1) {
                            response.send(Http::Code::Bad_Request, x.key() + " should be a number");
                        }
                    } else {
                        if (x.value().is_string() != 1) {
                            response.send(Http::Code::Bad_Request, x.key() + " should be a string");
                        }
                    }
                }
            }

            string name = reqBody.value("name", "nothing");
            int coffee = reqBody.value("coffee", 0);
            int milk = reqBody.value("milk", 0);
            int water = reqBody.value("water", 0);

            std::vector<std::string> chosenCoffee = esp.getChosenCoffee(name, milk, water, coffee);

            if (chosenCoffee[0] == "not_found") {
                response.send(Http::Code::Not_Found, "We don't have a " + name + " option in our menu.");
            } else if (chosenCoffee[0] == "bad_request") {
                response.send(Http::Code::Bad_Request, "You can't set water, milk or coffee for " + name + ".");
            } else if (chosenCoffee[0] == "empty_request") {
                response.send(Http::Code::No_Content, "Ok then! No coffee for you.");
            } else if (chosenCoffee[0] == "no_negatives") {
                response.send(Http::Code::Bad_Request, "You can't introduce negative values.");
            } else {

                std::vector<double> verifyCoffee = esp.verifyQuantity("coffee", coffee);
                std::vector<double> verifyWater = esp.verifyQuantity("water", water);
                std::vector<double> verifyMilk = esp.verifyQuantity("milk", milk);

                if(verifyCoffee[0] == 1 && verifyMilk[0] == 1 && verifyWater[0] == 1) {
                    esp.setDecrease("coffee", verifyCoffee[1]);
                    esp.setDecrease("water", verifyWater[1]);
                    esp.setDecrease("milk", verifyMilk[1]);
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
                            {"milk",   chosenCoffee[0]},
                            {"water",  chosenCoffee[1]},
                            {"coffee", chosenCoffee[2]}
                    };
                    std::string s = j.dump();
                    //Getting the current time to see how many coffees were made today
                    time_t now = time(0);
                    //Adds a coffee made to the counter
                    esp.count_coffee(now);
                    response.send(Http::Code::Ok, s);
                } else if(verifyCoffee[0] == 0 && verifyMilk[0] == 1 && verifyWater[0] == 1) {
                    response.send(Http::Code::Bad_Request, "Not enough coffee.");
                } else if(verifyCoffee[0] == 1 && verifyMilk[0] == 0 && verifyWater[0] == 1) {
                    response.send(Http::Code::Bad_Request, "Not enough milk.");
                } else if(verifyCoffee[0] == 1 && verifyMilk[0] == 1 && verifyWater[0] == 0) {
                    response.send(Http::Code::Bad_Request, "Not enough water.");
                } else if(verifyCoffee[0] == 0 && verifyMilk[0] == 0 && verifyWater[0] == 1) {
                    response.send(Http::Code::Bad_Request, "Not enough coffee and milk.");
                } else if(verifyCoffee[0] == 0 && verifyMilk[0] == 1 && verifyWater[0] == 0) {
                    response.send(Http::Code::Bad_Request, "Not enough coffee and water.");
                } else if(verifyCoffee[0] == 1 && verifyMilk[0] == 0 && verifyWater[0] == 0) {
                    response.send(Http::Code::Bad_Request, "Not enough milk and water.");
                } else if(verifyCoffee[0] == 0 && verifyMilk[0] == 0 && verifyWater[0] == 0) {
                    response.send(Http::Code::Bad_Request, "Not enough coffee, milk and water.");
                }

            }
        } else {
            response.send(Http::Code::No_Content, "Please enter the name of the coffee or the word your_choice followed by quantities of coffee, milk and water.");
        }
    }


    void showDefaultCoffee(const Rest::Request& request, Http::ResponseWriter response) {
        string property = "nothing";

        if (!request.body().empty()) {
            auto reqBody = json::parse(request.body());
            property = reqBody.value("property", "nothing");
        }

        std::vector<std::string> defaultCoffee = esp.getDefaultCoffee();

        Guard guard(EspressorLock);

        // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
        using namespace Http;
        response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Application, Json));

        json j = {
                {"defaultCoffee_name", defaultCoffee[0]},
                {"defaultCoffee_milk", defaultCoffee[1]},
                {"defaultCoffee_water", defaultCoffee[2]},
                {"defaultCoffee_coffee", defaultCoffee[3]}
        };


        std::string s = j.dump();
        response.send(Http::Code::Ok, s);

    }

    void setDefaultCoffee(const Rest::Request& request, Http::ResponseWriter response) {

        if (!request.body().empty()){
            auto reqBody = json::parse(request.body());

            if (reqBody.size() > 4) {
                response.send(Http::Code::Bad_Request, "Your request contains unnecessary properties. Check again!");
            }

            for (auto& x : reqBody.items()) {
                if ((x.key() != "name") && (x.key() != "coffee") && (x.key() != "water") && (x.key() != "milk")) {
                    response.send(Http::Code::Bad_Request, x.key() + " is not a valid property.");
                } else {
                    if (x.key() != "name") {
                        if (x.value().is_number() != 1) {
                            response.send(Http::Code::Bad_Request, x.key() + " should be a number");
                        }
                    } else {
                        if (x.value().is_string() != 1) {
                            response.send(Http::Code::Bad_Request, x.key() + " should be a string");
                        }
                    }
                }
            }

            string name = reqBody.value("name", "default");
            int milk = reqBody.value("milk", 0);
            int water = reqBody.value("water", 0);
            int coffee = reqBody.value("coffee", 0);

            if ((name == "espresso" || name == "flat_white" || name == "black_coffee" || name == "cappuccino") && (milk != 0 || water != 0 || coffee != 0)){
                response.send(Http::Code::Bad_Request,"This is a preset coffee!");
            }

            if (milk < 0 || water < 0 || coffee < 0){
                response.send(Http::Code::Bad_Request, "You can't introduce negative values.");
            }


            bool ok = esp.setDefaultCoffee(name, milk, water, coffee);

            if( ok == 1){
                response.send(Http::Code::Accepted, "Default coffe has been set as " + name + ".");
            }
            else{
                response.send(Http::Code::Not_Found, "Something went wrong!");
            }

            Guard guard(EspressorLock);

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Application, Json));

            json j = {
                    {"newDefaultCoffe", name},
                    {"defaultCoffee_milk", milk},
                    {"defaultCoffee_water", water},
                    {"defaultCoffee_coffee", coffee}
            };


            std::string s = j.dump();
            response.send(Http::Code::Ok, s);


        } else {
            response.send(Http::Code::No_Content, "Please enter the name of the coffee(name*/milk/water/coffee)");
        }

    }



    void makeAlarmCoffee(const Rest::Request& request, Http::ResponseWriter response){

        std::vector<std::string> defCoffee;
        defCoffee = esp.getDefaultCoffee(); //milk water coffee

        Guard guard(EspressorLock);

        // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
        using namespace Http;
        response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Application, Json));

        json j = {
                {"defaultCoffee_name", defCoffee[0]},
                {"defaultCoffee_water", defCoffee[1]},
                {"defaultCoffee_milk", defCoffee[2]},
                {"defaultCoffee_coffee", defCoffee[3]}
        };

        std::string s = j.dump();
        //Getting the current time to see how many coffees were made today
        time_t now = time(0);
        //Adds a coffee made to the counter
        esp.count_coffee(now);
        response.send(Http::Code::Ok, s);
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
        std::vector<std::string> getEspressor(int route = 0, string propertyName = "nothing") {
            std::vector<std::string> response;
            double water = 0;
            double milk = 0;
            double coffee = 0;
            int filters = 0;
            int coffees = 0;

            // function been called from "get number of coffees made today" route
            if (route == 1)  {
                coffees = round(espressor_details.coffees_today.number_today);
                response.emplace_back(std::to_string(coffees));
            }
                // function been called from "get current espressor quantities" route
            else {
                if (propertyName == "nothing") {
                    water = espressor_details.current_water.quant;
                    milk = espressor_details.current_milk.quant;
                    coffee = espressor_details.current_coffee.quant;
                    filters = round(espressor_details.filters_usage.quant);

                    // cout << water << " " << milk << " " << coffee << " " << filters;

                    response.emplace_back(std::to_string(water));
                    response.emplace_back(std::to_string(milk));
                    response.emplace_back(std::to_string(coffee));
                    response.emplace_back(std::to_string(filters));
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
                    } else if (propertyName == "filtersUsage") {
                        filters = round(espressor_details.filters_usage.quant);
                        response.emplace_back(std::to_string(filters));
                    } else {
                        response.emplace_back("property_not_found");
                    }
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

        std::vector<std::string> getBoilWaterTime(int amountWater) {
            std::vector<std::string> response;
            int time = 0;
            int unit = 2;
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

        bool same_day(time_t day){
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
            if (current_year!=new_year)
            {
                return false;
            }
            else if(current_year==new_year && current_month!=new_month)
            {
                return false;
            } else if (current_day!=new_day && current_month==new_month && current_year==new_year)
            {
                return false;
            }
            return true;
        }

//        If the day changed and no coffees were made, sets counter to 0
        void reset_coffee_counter(time_t day){
            if (!same_day(day)) {
                espressor_details.coffees_today.number_today = 0;
            }
        }

//        Counts the number of coffees were made
        void count_coffee(time_t day){
            if (same_day(day))
            {
                espressor_details.coffees_today.number_today += 1;
            }
            else
            {
                espressor_details.coffees_today.number_today = 1;
                espressor_details.coffees_today.day = day;
            }
        }

        // Getter for chosenCoffee route
        std::vector<std::string> getChosenCoffee(string name, double milk, double water, double coffee) {
            std::vector<std::string> response;
            if (name != "your_choice" && name != "back_coffee" && name != "espresso" && name != "flat_white" && name != "cappuccino") {
                response.emplace_back("not_found");
            } else {
                if (milk < 0 || water < 0 || coffee < 0) {
                    response.emplace_back("no_negatives");
                } else {
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
                }
            }


            return response;
        }


        std::vector<std::string> getDefaultCoffee(){
            std::vector<std::string> response;
            response.emplace_back(defaultCoffee.name);
            response.emplace_back(std::to_string(defaultCoffee.ingredients.milk.quant));
            response.emplace_back(std::to_string(defaultCoffee.ingredients.water.quant));
            response.emplace_back(std::to_string(defaultCoffee.ingredients.coffee.quant));
            return response;
        }



        bool setDefaultCoffee(string name_wanted, double milk_wanted = 0, double water_wanted = 0, double coffee_wanted = 0) {

            bool ok = 0;
            if (name_wanted == "black_coffee"){
                defaultCoffee.name = "black_coffee";
                defaultCoffee.ingredients = possible_choices.black_coffee;
                ok = 1;
            }
            else if (name_wanted == "flat_white")
            {
                defaultCoffee.name = "flat_white";
                defaultCoffee.ingredients = possible_choices.flat_white;
                ok = 1;
            }
            else if (name_wanted == "espresso")
            {
                defaultCoffee.name = "espresso";
                defaultCoffee.ingredients = possible_choices.espresso;
                ok = 1;
            }
            else if (name_wanted == "cappuccino")
            {
                defaultCoffee.name = "cappuccino";
                defaultCoffee.ingredients = possible_choices.cappuccino;
                ok = 1;
            }
            else if (milk_wanted != 0 || water_wanted != 0 || coffee_wanted != 0)
            {
                defaultCoffee.name = name_wanted;
                defaultCoffee.ingredients = {milk_wanted, water_wanted, coffee_wanted};
                ok = 1;
            }

            return ok;  // daca se returneaza ok = 0 inseamnaca eroare si trebuie luata in considerare


        }


        std::vector<double> verifyQuantity(string property, int quantity) {
            std::vector<double> response;

            if(property == "coffee") {
                if (espressor_details.current_coffee.quant - quantity >= 0) {
                    response.emplace_back(double(1));
                    response.emplace_back(double(espressor_details.current_coffee.quant - quantity));
                } else {
                    response.emplace_back(double(0));
                    response.emplace_back(double(espressor_details.current_coffee.quant - quantity));
                }
            } else if(property == "water") {
                if(espressor_details.current_water.quant - quantity >= 0) {
                    response.emplace_back(double (1));
                    response.emplace_back(double (espressor_details.current_water.quant - quantity));
                }
                else {
                    response.emplace_back(double (0));
                    response.emplace_back(double (espressor_details.current_water.quant - quantity));
                }
            } else if(property == "milk") {
                if(espressor_details.current_milk.quant - quantity >= 0) {
                    response.emplace_back(double (1));
                    response.emplace_back(double (espressor_details.current_milk.quant - quantity));
                }
                else {
                    response.emplace_back(double (0));
                    response.emplace_back(double (espressor_details.current_milk.quant - quantity));
                }
            }

            return response;
        }

        void setDecrease ( string property, double quantity) {
            if( property == "coffee") {
                espressor_details.current_coffee.quant = quantity;
            } else if( property == "milk") {
                espressor_details.current_milk.quant = quantity;
            } else if ( property == "water") {
                espressor_details.current_water.quant = quantity;
            }
        }

    private:
//        double boiling_water = 5; // in minutes

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


        struct defCoffee {
            string name = "black_coffee";
            choices possible_choices;
            coffee ingredients = possible_choices.black_coffee;
        } defaultCoffee;

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


    // Code that waits for the shutdown signal for the server
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