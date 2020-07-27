#ifndef ui_h
#define ui_h

// #include "Arduino.h"
#include <ESPAsyncWebServer.h>
#include "ArduinoJson.h"
#include "JeeUI2.h"
#include "../../include/LList.h"

class frameSend {
    public:
        virtual ~frameSend(){};
        virtual void send(const String &data){};
        virtual void flush(){}
        virtual void close(){}
};

class frameSendAll: public frameSend {
    private:
        AsyncWebSocket *ws;
    public:
        frameSendAll(AsyncWebSocket *server){ ws = server; }
        ~frameSendAll() { ws = nullptr; }
        void send(const String &data){ if (data.length()) ws->textAll(data); };
        void close(){ ws->closeAll(); }
};

class frameSendClient: public frameSend {
    private:
        AsyncWebSocketClient *cl;
    public:
        frameSendClient(AsyncWebSocketClient *client){ cl = client; }
        ~frameSendClient() { cl = nullptr; }
        void send(const String &data){ if (data.length()) cl->text(data); };
        void close(){ cl->close(); }
};

class frameSendHttp: public frameSend {
    private:
        AsyncWebServerRequest *req;
        AsyncResponseStream *stream;
    public:
        frameSendHttp(AsyncWebServerRequest *request){
            req = request;
            stream = req->beginResponseStream(FPSTR("application/json"));
            stream->addHeader(FPSTR("Cache-Control"), FPSTR("no-cache, no-store, must-revalidate"));
        }
        ~frameSendHttp() { /* delete stream; */ req = nullptr; }
        void send(const String &data){
            if (!data.length()) return;
            stream->print(data);
        };
        void flush(){
            req->send(stream);
        };
};

class Interface {
    typedef struct section_stack_t{
      JsonArray block;
      String name;
      int idx;
    } section_stack_t;

    DynamicJsonDocument json;
    LList<section_stack_t*> section_stack;
    frameSend *send_hndl;
    jeeui2 *jee;

    public:
        Interface(jeeui2 *j, AsyncWebSocket *server, size_t size = 3000): json(size), section_stack(){
            jee = j;
            send_hndl = new frameSendAll(server);
        }
        Interface(jeeui2 *j, AsyncWebSocketClient *client, size_t size = 3000): json(size), section_stack(){
            jee = j;
            send_hndl = new frameSendClient(client);
        }
        Interface(jeeui2 *j, AsyncWebServerRequest *request, size_t size = 3000): json(size), section_stack(){
            jee = j;
            send_hndl = new frameSendHttp(request);
        }
        ~Interface(){
            delete send_hndl;
            send_hndl = nullptr;
            jee = nullptr;
        }

        void close(){ send_hndl->close(); }

        void json_frame_value();
        void json_frame_interface(const String &name = "");
        bool json_frame_add(JsonObject obj);
        void json_frame_next();
        void json_frame_clear();
        void json_frame_flush();
        void json_frame_send();

        void json_section_menu();
        void json_section_content();
        void json_section_line(const String &name = "");
        void json_section_main(const String &name, const String &label);
        void json_section_hidden(const String &name, const String &label);
        void json_section_begin(const String &name, const String &label = "", bool main = false, bool hidden = false, bool line = false);
        void json_section_begin(const String &name, const String &label, bool main, bool hidden, bool line, JsonObject obj);
        void json_section_end();

        void value(const String &id, bool html = false);
        void value(const String &id, const String &val, bool html = false);
        void hidden(const String &id);
        void hidden(const String &id, const String &value);
        void constant(const String &id, const String &label);
        void constant(const String &id, const String &value, const String &label);
        void text(const String &id, const String &label);
        void text(const String &id, const String &value, const String &label);
        void password(const String &id, const String &label);
        void password(const String &id, const String &value, const String &label);
        void number(const String &id, const String &label, int min = 0, int max = 0);
        void number(const String &id, int value, const String &label, int min = 0, int max = 0);
        void number(const String &id, const String &label, float step, int min = 0, int max = 0);
        void number(const String &id, float value, const String &label, float step, int min = 0, int max = 0);
        void time(const String &id, const String &label);
        void time(const String &id, const String &value, const String &label);
        void date(const String &id, const String &label);
        void date(const String &id, const String &value, const String &label);
        void datetime(const String &id, const String &label);
        void datetime(const String &id, const String &value, const String &label);
        void email(const String &id, const String &label);
        void email(const String &id, const String &value, const String &label);
        void range(const String &id, int min, int max, float step, const String &label, bool directly = false);
        void range(const String &id, int value, int min, int max, float step, const String &label, bool directly);
        void select(const String &id, const String &label, bool directly = false);
        void select(const String &id, const String &value, const String &label, bool directly = false);
        void option(const String &value, const String &label);
        /**
         * элемент интерфейса checkbox
         * @param directly - значение чекбокса при изменении сразу передается на сервер без отправки формы
         */
        void checkbox(const String &id, const String &label, bool directly = false);
        void checkbox(const String &id, const String &value, const String &label, bool directly = false);
        void color(const String &id, const String &label);
        void color(const String &id, const String &value, const String &label);
        void textarea(const String &id, const String &label);
        void textarea(const String &id, const String &value, const String &label);
        void file(const String &name, const String &action, const String &label);
        void button(const String &id, const String &label, const String &color = "");
        void button_submit(const String &section, const String &label, const String &color = "");
        void button_submit_value(const String &section, const String &value, const String &label, const String &color = "");
        void spacer(const String &label = "");
        void comment(const String &label = "");
};

#endif