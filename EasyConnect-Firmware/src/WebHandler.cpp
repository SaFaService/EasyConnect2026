#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "GestioneMemoria.h"
#include "Pins.h"
#include "Calibration.h" // Inclusione modulo calibrazione

// Riferimenti esterni
extern Preferences memoria;
extern Impostazioni config;
extern bool listaPerifericheAttive[101];
extern int statoInternet;
extern bool scansioneInCorso;
extern float currentDeltaP; // Variabile globale per DeltaP
extern DatiSlave databaseSlave[101];

// Riferimento alla versione FW definita nel main
extern const char* FW_VERSION;

// Variabili gestione AP
unsigned long lastWifiCheck = 0;
bool apEnabled = true;
int wifiRetryCount = 0; // Contatore tentativi riconnessione WiFi


// Oggetti Globali
WebServer server(80);
DNSServer dnsServer;

// --- LOGO (Base64 Placeholder) ---
const char* LOGO_IMG = "data:image/jpeg;base64,/9j/4QlrRXhpZgAATU0AKgAAAAgABwESAAMAAAABAAEAAAEaAAUAAAABAAAAYgEbAAUAAAABAAAAagEoAAMAAAABAAIAAAExAAIAAAAgAAAAcgEyAAIAAAAUAAAAkodpAAQAAAABAAAAqAAAANQACwBoAAAnEAALAGgAACcQQWRvYmUgUGhvdG9zaG9wIDI2LjEwIChXaW5kb3dzKQAyMDI2OjAxOjIyIDEwOjM1OjA5AAAAAAOgAQADAAAAAf//AACgAgAEAAAAAQAAAJagAwAEAAAAAQAAADIAAAAAAAAABgEDAAMAAAABAAYAAAEaAAUAAAABAAABIgEbAAUAAAABAAABKgEoAAMAAAABAAIAAAIBAAQAAAABAAABMgICAAQAAAABAAAIMQAAAAAAAABIAAAAAQAAAEgAAAAB/9j/7QAMQWRvYmVfQ00AAv/uAA5BZG9iZQBkgAAAAAH/2wCEAAwICAgJCAwJCQwRCwoLERUPDAwPFRgTExUTExgRDAwMDAwMEQwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwBDQsLDQ4NEA4OEBQODg4UFA4ODg4UEQwMDAwMEREMDAwMDAwRDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDP/AABEIADIAlgMBIgACEQEDEQH/3QAEAAr/xAE/AAABBQEBAQEBAQAAAAAAAAADAAECBAUGBwgJCgsBAAEFAQEBAQEBAAAAAAAAAAEAAgMEBQYHCAkKCxAAAQQBAwIEAgUHBggFAwwzAQACEQMEIRIxBUFRYRMicYEyBhSRobFCIyQVUsFiMzRygtFDByWSU/Dh8WNzNRaisoMmRJNUZEXCo3Q2F9JV4mXys4TD03Xj80YnlKSFtJXE1OT0pbXF1eX1VmZ2hpamtsbW5vY3R1dnd4eXp7fH1+f3EQACAgECBAQDBAUGBwcGBTUBAAIRAyExEgRBUWFxIhMFMoGRFKGxQiPBUtHwMyRi4XKCkkNTFWNzNPElBhaisoMHJjXC0kSTVKMXZEVVNnRl4vKzhMPTdePzRpSkhbSVxNTk9KW1xdXl9VZmdoaWprbG1ub2JzdHV2d3h5ent8f/2gAMAwEAAhEDEQA/APVEklm5fVz020Nz6z6DzFeTWJE/uW1/SY/+r9NNlIRFnQd18Mcpmoi5fu9T/ddJJVcfqvTsofoMmtxP5sw7/MdDlaBB4Mogg6gg+S2UZRNSBifEUpJRc9jSA5wBJgAnkqSKFJJJJKUkkkkpSSSSSlJJJJKUkkkkpSSSSSlJJJJKf//Q9UQsrFpy8d+Pe3dXYII/78P5TUVJIgEUeqQSCCDRGoL59f0uzH6kcG48H2v/AHmn6Lwi9V6aMHZ6dhdujQE9/gt762YRsxGZ1eluMfcRzsd/5B6ofVnCszsk5uSS+rGMVg8GyJn/AK21Z0sIGQ4gNZG4y/dg7cOaMsMeYMqjAGOSH7+Tp/jNnp+Fi9BwT1XqQLsjQBoG5zA47RXW3863/SLomPY9gsYQ5jgHNcOCDqCFmXvry+tMpsLfs/Tm+o8OIh19oLKmw7/Q4/qP/wCvVp+ivbSzI6XO77C79CZmaLJfj6/8H78f/rK0YYowgIx6an6uLlzTy5DOZu/w/qhtdO6li9SxRlYriWElrmnRzXDlj2/muT5mdXiGprmPsfe5zK2sAJLmsfdHuLfpNqWLhVW4PTsLq+K0vb9nrbn47ebK2jTIrb+dk43/AINT+i/0Sv59tV2T0e6lwfXZkFzHt1BBovLXBPMRfhr+DHenjou7rjmlod0/LaXmGAtrBJjdtb+n93tanf1vY1u/BymvseK66y1m5x2vtOz9Lt9jK/cl1X+ndK/8NO/88ZCbrFnpZHTrAx1my952MEuP6C/2sB2paaabhWuuq/7crrIOVi5OJUSG+tcxvpgnRvqPqfb6fu/Pf7FpEgAkmANSSsPO6hb1Kq/pGNiWsyb6trzkBrGMrs3V+ufe51m337WV/no/WDtw6OlMsLX5pFBsJgipo3ZVs/8AEt2f8Zahw7dP4Kvfq2+ndRxupY32nGJ2bnMIcIILT3H8pv6Rn/BolOXVdkZGO0EPxXNbYTwS9otbt/suWfWasLrLW0lv2XPYGbWnRt9Lf0f/AG/it2/+gyJ0/wD5V6r/AMZT/wCea0iBr5WE3t5tx2XU3MrwyD6ttb7Wn83awsY6f5X6VqMs27/xQ4v/AIVv/wCrxlpIEbeSh1aOD1jFzXsrrD63W1i6r1ABvZOx/pwXe6p3863+ojZubThVCy0OcXuFddbBL3vd9Gutuix8LEbf9WsS5tjaMjEYbsfIdwxzS7d6n/AvZ7L/APg1Y6Uf2llO6ne9jnUAVY9NZLm1bmMsusdvaz9Ndv8A3f6P/wAZYnGI1PQIBOni7CSSSYuf/9H1RJJJJSLLpbkYttDtW2sc0/MQqnSqP2f0epuwueyr1HsaPc55HqPaP5W72rQSQ4RxCXUCl/HLgOP9EyEvqNHJ6f0TFOK23qONVfm3k3ZD7GNcQ953+mHOB9lP80z+opZGCMTKw8np9IZWwuoyKamho9K07vU2t/0N+2z+p6y1Ek7iNsdBp9Gqsp6ViVWtLLGVMa9h5BA4KzrOmZWL1XDGM3d03133lo/wDzXc2wN/7r3vs3/8Fb/xi3UkuI2fFVbeDQ6jTbZl9NfWwubVkOdYRw1vo3M3O/tuapZ9VtmX097Glzar3OsI4a003M3O/tuarqSV7eCqc7q2PeDT1DEaX5WGSfTbzbU7+kY/9ZzR6lX/AA1bEOvAZ1DPyMrqGMH01xRiV3NB9o/SXX7Hbv52x2z+pStVJLiNKpzM3ouIcO1uBRVjZI22UvrY1p9So+rTu27fbvVXFzsnHzMvIuwMojL9GxoYwO2kVMZZW73N9zHjat1JES0o6qr6OHfk5WVl4+Th4WRXkYwfLb2Cuuyt4b6tHq7n+nZuYyyn+WxHs6j1HJYaMPBuoveNpuyQ1tdc6ep7H2Ov2fmsr+mtVJLiHbZVeLjHpr7Ps/SAxzemYbGG97/8O4fzdH/F7m+tlf8AbX+kR87HuxspvU8NhsdDa8zHZzbXPssYP9PjbvZ/pKt9X+jWkklxFVKSSSTUv//S9USXyskkp+qUl8rJJKfqlJfKySSn6pSXyskkp+qUl8rJJKfqlJfKySSn6pSXyskkp+qUl8rJJKfqlJfKySSn6pSXyskkp//Z/+0RtFBob3Rvc2hvcCAzLjAAOEJJTQQlAAAAAAAQAAAAAAAAAAAAAAAAAAAAADhCSU0EOgAAAAAA8wAAABAAAAABAAAAAAALcHJpbnRPdXRwdXQAAAAFAAAAAFBzdFNib29sAQAAAABJbnRlZW51bQAAAABJbnRlAAAAAENscm0AAAAPcHJpbnRTaXh0ZWVuQml0Ym9vbAAAAAALcHJpbnRlck5hbWVURVhUAAAABgBMAEEAQgBFAEwAAAAAAA9wcmludFByb29mU2V0dXBPYmpjAAAADgBJAG0AcABvAHMAdABhACAAcAByAG8AdgBhAAAAAAAKcHJvb2ZTZXR1cAAAAAEAAAAAQmx0bmVudW0AAAAMYnVpbHRpblByb29mAAAACXByb29mQ01ZSwA4QklNBDsAAAAAAi0AAAAQAAAAAQAAAAAAEnByaW50T3V0cHV0T3B0aW9ucwAAABcAAAAAQ3B0bmJvb2wAAAAAAENsYnJib29sAAAAAABSZ3NNYm9vbAAAAAAAQ3JuQ2Jvb2wAAAAAAENudENib29sAAAAAABMYmxzYm9vbAAAAAAATmd0dmJvb2wAAAAAAEVtbERib29sAAAAAABJbnRyYm9vbAAAAAAAQmNrZ09iamMAAAABAAAAAAAAUkdCQwAAAAMAAAAAUmQgIGRvdWJAb+AAAAAAAAAAAABHcm4gZG91YkBv4AAAAAAAAAAAAEJsICBkb3ViQG/gAAAAAAAAAAAAQnJkVFVudEYjUmx0AAAAAAAAAAAAAAAAQmxkIFVudEYjUmx0AAAAAAAAAAAAAAAAUnNsdFVudEYjUHhsQFIGZkAAAAAAAAAKdmVjdG9yRGF0YWJvb2wBAAAAAFBnUHNlbnVtAAAAAFBnUHMAAAAAUGdQQwAAAABMZWZ0VW50RiNSbHQAAAAAAAAAAAAAAABUb3AgVW50RiNSbHQAAAAAAAAAAAAAAABTY2wgVW50RiNQcmNAWQAAAAAAAAAAABBjcm9wV2hlblByaW50aW5nYm9vbAAAAAAOY3JvcFJlY3RCb3R0b21sb25nAAAAAAAAAAxjcm9wUmVjdExlZnRsb25nAAAAAAAAAA1jcm9wUmVjdFJpZ2h0bG9uZwAAAAAAAAALY3JvcFJlY3RUb3Bsb25nAAAAAAA4QklNA+0AAAAAABAASBmZAAEAAgBIGZkAAQACOEJJTQQmAAAAAAAOAAAAAAAAAAAAAD+AAAA4QklNBA0AAAAAAAQAAABaOEJJTQQZAAAAAAAEAAAAHjhCSU0D8wAAAAAACQAAAAAAAAAAAQA4QklNJxAAAAAAAAoAAQAAAAAAAAACOEJJTQP1AAAAAABIAC9mZgABAGxmZgAGAAAAAAABAC9mZgABAKGZmgAGAAAAAAABADIAAAABAFoAAAAGAAAAAAABADUAAAABAC0AAAAGAAAAAAABOEJJTQP4AAAAAABwAAD/////////////////////////////A+gAAAAA/////////////////////////////wPoAAAAAP////////////////////////////8D6AAAAAD/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c3ludGF4LW5zIyI+IDxyZGY6RGVzY3JpcHRpb24gcmRmOmFib3V0PSIiIHhtbG5zOnhtcD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wLyIgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIiB4bWxuczpwaG90b3Nob3A9Imh0dHA6Ly9ucy5hZG9iZS5jb20vcGhvdG9zaG9wLzEuMC8iIHhtbG5zOnhtcE1NPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvbW0vIiB4bWxuczpzdEV2dD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlRXZlbnQjIiB4bXA6Q3JlYXRvclRvb2w9IkFkb2JlIFBob3Rvc2hvcCAyNi4xMCAoV2luZG93cykiIHhtcDpDcmVhdGVEYXRlPSIyMDI2LTAxLTIyVDEwOjMzOjA2KzAxOjAwIiB4bXA6TW9kaWZ5RGF0ZT0iMjAyNi0wMS0yMlQxMDozNTowOSswMTowMCIgeG1wOk1ldGFkYXRhRGF0ZT0iMjAyNi0wMS0yMlQxMDozNTowOSswMTowMCIgZGM6Zm9ybWF0PSJpbWFnZS9qcGVnIiBwaG90b3Nob3A6Q29sb3JNb2RlPSIzIiB4bXBNTTpJbnN0YW5jZUlEPSJ4bXAuaWlkOjRlODE2NjU3LWNlODktNjc0Ni05NTA2LTM5N2QwOTg2YjA1NCIgeG1wTU06RG9jdW1lbnRJRD0iYWRvYmU6ZG9jaWQ6cGhvdG9zaG9wOjEwNDZmZWEwLWZiN2QtODE0Yi1hMWI4LTE2OWJmZGY3MWMwYiIgeG1wTU06T3JpZ2luYWxEb2N1bWVudElEPSJ4bXAuZGlkOmY2OGY0ZmU0LTkxZjQtY2Q0MS04ZjQ2LTk3ZDA5MTU1ZDZjZCI+IDx4bXBNTTpIaXN0b3J5PiA8cmRmOlNlcT4gPHJkZjpsaSBzdEV2dDphY3Rpb249ImNyZWF0ZWQiIHN0RXZ0Omluc3RhbmNlSUQ9InhtcC5paWQ6ZjY4ZjRmZTQtOTFmNC1jZDQxLThmNDYtOTdkMDkxNTVkNmNkIiBzdEV2dDp3aGVuPSIyMDI2LTAxLTIyVDEwOjMzOjA2KzAxOjAwIiBzdEV2dDpzb2Z0d2FyZUFnZW50PSJBZG9iZSBQaG90b3Nob3AgMjYuMTAgKFdpbmRvd3MpIi8+IDxyZGY6bGkgc3RFdnQ6YWN0aW9uPSJjb252ZXJ0ZWQiIHN0RXZ0OnBhcmFtZXRlcnM9ImZyb20gYXBwbGljYXRpb24vdm5kLmFkb2JlLnBob3Rvc2hvcCB0byBpbWFnZS9qcGVnIi8+IDxyZGY6bGkgc3RFdnQ6YWN0aW9uPSJzYXZlZCIgc3RFdnQ6aW5zdGFuY2VJRD0ieG1wLmlpZDo0ZTgxNjY1Ny1jZTg5LTY3NDYtOTUwNi0zOTdkMDk4NmIwNTQiIHN0RXZ0OndoZW49IjIwMjYtMDEtMjJUMTA6MzU6MDkrMDE6MDAiIHN0RXZ0OnNvZnR3YXJlQWdlbnQ9IkFkb2JlIFBob3Rvc2hvcCAyNi4xMCAoV2luZG93cykiIHN0RXZ0OmNoYW5nZWQ9Ii8iLz4gPC9yZGY6U2VxPiA8L3htcE1NOkhpc3Rvcnk+IDwvcmRmOkRlc2NyaXB0aW9uPiA8L3JkZjpSREY+IDwveDp4bXBtZXRhPiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIDw/eHBhY2tldCBlbmQ9InciPz7/7gAOQWRvYmUAZEAAAAAB/9sAhAABAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAgICAgICAgICAgIDAwMDAwMDAwMDAQEBAQEBAQEBAQECAgECAgMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwP/wAARCAAyAJYDAREAAhEBAxEB/90ABAAT/8QBogAAAAYCAwEAAAAAAAAAAAAABwgGBQQJ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pBP2Dq17FJJc7UyISqTEtTyHhSCp/Mgfaekz8sOvt6xSdefJLpvC12f7j+PeTr69tn4ZacZTuLpjcxoIO3emofuJYKeoy2axmOp8vt8TOI4tyYeiuUSWUl3Zbm3Iu9pv5AthdADUeEUq18OX1oCSr0z4bt5gdNbnBN/i+4WqlruA/COMkZprj+0gBl9GUep6RGB6KxPyP777k7c+RvTOIzex9pU+C6W+POy+2tpYnNRf3YxtPR7v7I7Vi25m4MlS0NVv/e2TgxtLM6R1Yx224jpRZ2DKJNyfa9ssbHar9luHJlneNiO41WOOopUIgLEcNUh9OmUslv767ur+0DQrSOJXUHA7nehr8TEAedEHr099z/CvqWo6f33jvj71R1f1F21T/wB29/db7p2JsbbO0K2HtHqjNpvjrI5OtwdBjZqnEnctH9pVRyOUahrqlLWka7e37/erfW77nezT2J1JIruzjw5BokoCTnTkfNV9Or3e0WptZlsbaOK6FGRlUL3odSVoBiuD8ifXoAuru7+xOue4fkH2JvL4i/KyeHvtegN/4ih2X1vi9zJt2px/QGxdubq2pnaubdWJEOd2xurH1VDOqI8btBrR2VgfZne2FrdWO12tvvdnW28ZCWcrqrM7Kw7ThlII+3pFbXc9vdX1xNtdzSfwmAVQaUjUEHuGQ1Qfs6dN9dkdn9tdudQdp9K/Gj5EbV7V6dx/YMM2I7w2Ti+tOuO1urt5UWDXe/V1Vv8Ap9ybiXam9azJ4DF5fbVXU0klAcnivtqpoaaplnibtrazsrG+s9w3W2eznKZicu8cik6JNGldSUZlkANdLVFSADeee4urq1ubTb51uYQ2JFCq6NTUmqpoxIUoSKVFDQEkCjuH5FfIztHC1+wOk/ip3R1d2RnKSbD1XZXyApNibX6x6lauhejqt3mo27vndmT7SyG35JDNQYvC08kGSmRFmq6SBnmRJFtW1Wbrc7hvME1opr4cOtpJKZ09yKIw3As5BUVorGgKh7+/uUMFpts0c7CheTSESuK4Zi5HEBRnzIGekjN8csvuVuovhdR4HceN+H3x+2TsTIds7m3LTQo3yZ3Bi4hLtDqqik1lq/aUGaxZ3DvuoEaw5CeSkxKa4J8iqvjdY4vrt/aRTvtzI4jVf9AU/FIfRqHRCOKjU/EJ019A0n0u0qjDaoEUuT/opHwp81qNUnkcLwLdCb3lsHenVfaeE+V3R+2cxurJy0m3+v8A5HdQbVhpf4j2/wBVU9bLTbf3ntzHzSU9LWdsdIVGVmq8eC8cuXwElbiy5k+w8KPbrm3vLKTZtxlVFqzwStwjkp3IxyfDloA3krhXpTVVReQTW1ym52cZZqBZY1/GnkwH8cdaj+Jar/DQ7QceMyWfT49dtD+TTw1vFbya7f2bar8Wv7D/AMujjr//1N/E/U/65/3v37r3XXv3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de6979Tr3Xvfuvde9+6912Pof8AW/4ke/de6//V38T9T/rn/e/fuvdde/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917rsfQ/63/Ej37r3X//2Q==";

// --- FAVICON (Base64 - Pallino Blu) ---
const char* FAVICON_IMG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAFklEQVR42mNk+M9AAzDCo/6j/qP+QwwA+bg/wX44w18AAAAASUVORK5CYII=";

// --- GENERAZIONE HTML DASHBOARD ---
String getDashboardHTML() {
    // Inizio costruzione pagina HTML (Start building HTML page)
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Antralux Rewamping</title>";
    html += "<style>";
    
    // Definizione stili CSS (CSS Style definitions)
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; background-color: #f0f2f5; color: #333; }";
    html += ".header { background-color: #fff; padding: 15px; text-align: center; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += ".logo { max-height: 60px; margin-bottom: 10px; }";
    html += ".container { padding: 20px; max-width: 800px; margin: 0 auto; }";
    
    // Stile delle Card / Riquadri (Card Style)
    html += ".card { background: #fff; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); }";
    html += ".card h3 { margin-top: 0; color: #0056b3; border-bottom: 2px solid #f0f2f5; padding-bottom: 10px; text-align: center; }";
    
    // Griglia per i dati (Data Grid)
    html += ".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; }";
    html += ".info-item { background: #f9f9f9; padding: 10px; border-radius: 5px; text-align: center; }";
    html += ".info-label { font-size: 0.85em; color: #666; display: block; }";
    html += ".info-value { font-size: 1.2em; font-weight: bold; color: #333; }";
    
    // Pulsanti (Buttons)
    html += ".menu-bar { display: flex; gap: 10px; margin-top: 20px; }";
    html += ".btn { flex: 1; padding: 15px; text-align: center; text-decoration: none; color: white; border-radius: 8px; font-weight: bold; transition: opacity 0.3s; }";
    html += ".btn-calib { background-color: #17a2b8; }";
    html += ".btn-wifi { background-color: #6c757d; }";
    html += ".btn:hover { opacity: 0.9; }";
    html += ".dot { height: 15px; width: 15px; background-color: #bbb; border-radius: 50%; display: inline-block; vertical-align: middle; }";
    html += ".dot-green { background-color: #28a745; }";
    html += ".dot-red { background-color: #dc3545; }";
    html += "</style></head><body>";

    // Intestazione con Logo (Header with Logo)
    html += "<div class='header'>";
    html += "<img src='" + String(LOGO_IMG) + "' class='logo' alt='Antralux Logo'>";
    html += "</div>";

    html += "<div class='container'>";

    // --- CALCOLI DATI SISTEMA (SYSTEM DATA CALCULATIONS) ---
    int countPeriferiche = 0; // Contatore periferiche (Device counter)
    float pressG1 = 0.0;      // Pressione Gruppo 1 (Pressure Group 1)
    float pressG2 = 0.0;      // Pressione Gruppo 2 (Pressure Group 2)
    bool foundG1 = false;     // Trovato Gruppo 1? (Found Group 1?)
    bool foundG2 = false;     // Trovato Gruppo 2? (Found Group 2?)

    // Scansioniamo l'array per contare e trovare le pressioni
    // (Scan array to count and find pressures)
    for(int i=1; i<=100; i++) {
        if(listaPerifericheAttive[i]) {
            countPeriferiche++;
            // Prendiamo la pressione del primo slave trovato per ogni gruppo
            // (Take pressure of the first slave found for each group)
            if (databaseSlave[i].grp == 1 && !foundG1) {
                pressG1 = databaseSlave[i].p;
                foundG1 = true;
            }
            else if (databaseSlave[i].grp == 2 && !foundG2) {
                pressG2 = databaseSlave[i].p;
                foundG2 = true;
            }
        }
    }
    
    // Calcolo Delta P = P1 - P2
    String deltaPStr = "--";
    if (foundG1 && foundG2) {
        deltaPStr = String(pressG1 - pressG2, 0) + " Pa";
    } else {
        deltaPStr = "In attesa dati (Waiting data)";
    }
    
    // --- CONTROLLO SOGLIE CALIBRAZIONE ---
    String warningMsg = checkThresholds((foundG1 && foundG2) ? (pressG1 - pressG2) : 0.0);
    String warningHtml = (warningMsg != "") ? "<div style='color:red; font-weight:bold; margin-top:5px;'>" + warningMsg + "</div>" : "";

    // --- DATI CONNESSIONE (CONNECTION DATA) ---
    String apClass = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) ? "dot-green" : "dot-red";
    String internetClass = (statoInternet == 2) ? "dot-green" : "dot-red";
    String ssidName = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Non connesso";
    String ipAddr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "--";

    // --- RIQUADRO MASTER (MASTER CARD) ---
    html += "<div class='card'>";
    // Titolo: Seriale Master (Title: Master Serial)
    html += "<h3>Centralina: " + String(config.serialeID) + " (v" + String(FW_VERSION) + ")</h3>";
    html += "<div class='info-grid'>";
    
    // Info Connessione (Connection Info)
    html += "<div class='info-item'><span class='info-label'>MODALIT&Agrave; AP</span><span class='info-value'><span class='dot " + apClass + "'></span></span></div>";
    html += "<div class='info-item'><span class='info-label'>INTERNET</span><span class='info-value'><span class='dot " + internetClass + "'></span></span></div>";
    html += "<div class='info-item'><span class='info-label'>RETE WIFI</span><span class='info-value'>" + ssidName + "</span></div>";
    html += "<div class='info-item'><span class='info-label'>IP RETE</span><span class='info-value'>" + ipAddr + "</span></div>";

    // Numero Periferiche Rilevate (Number of Detected Peripherals)
    html += "<div class='info-item'><span class='info-label'>PERIFERICHE (DEVICES)</span>";
    html += "<span class='info-value'>" + String(countPeriferiche) + "</span></div>";
    
    // Valore Delta Pressione (Delta Pressure Value)
    html += "<div class='info-item'><span class='info-label'>DELTA PRESSIONE</span>"; // Aggiunto ID per aggiornamento JS (Added ID for JS update)
    html += "<span class='info-value' id='deltaP'>" + deltaPStr + "</span>" + warningHtml + "</div>";
    
    html += "</div></div>";

    // --- RIQUADRI SLAVE (SLAVE CARDS) ---
    if (countPeriferiche > 0) {
        for (int i = 1; i <= 100; i++) {
            if (listaPerifericheAttive[i]) {
                html += "<div class='card'>";
                // Titolo: Seriale Slave (Title: Slave Serial)
                html += "<h3>Sensore: " + String(databaseSlave[i].sn) + " (v" + String(databaseSlave[i].version) + ")</h3>";
                html += "<div class='info-grid'>";
                
                // Indirizzo IP / ID 485 (IP Address)
                html += "<div class='info-item'><span class='info-label'>IP (ADDR)</span><span class='info-value'>" + String(i) + "</span></div>";
                // Gruppo (Group)
                html += "<div class='info-item'><span class='info-label'>GRUPPO (GROUP)</span><span class='info-value'>" + String(databaseSlave[i].grp) + "</span></div>";
                // Modalità (Mode) - Placeholder, dato non disponibile dallo slave (Data not available from slave)
                html += "<div class='info-item'><span class='info-label'>MODALIT&Agrave;</span><span class='info-value'>--</span></div>";
                // Pressione (Pressure)
                html += "<div class='info-item'><span class='info-label'>PRESSIONE</span><span class='info-value' id='press-" + String(i) + "'>" + String(databaseSlave[i].p, 0) + " Pa</span></div>";
                // Temperatura (Temperature)
                html += "<div class='info-item'><span class='info-label'>TEMP</span><span class='info-value' id='temp-" + String(i) + "'>" + String(databaseSlave[i].t, 1) + " &deg;C</span></div>";
                
                html += "</div></div>";
            }
        }
    } else {
        // Nessuna periferica (No devices)
        html += "<div class='card'><p style='text-align:center; color:#666;'>Nessuna periferica rilevata.</p></div>";
    }

    // --- TASTI MENU (MENU BUTTONS) ---
    html += "<div class='menu-bar'>";
    html += "<a href='/calibrazione' class='btn btn-calib'>CALIBRAZIONE</a>";
    html += "<a href='/wifi' class='btn btn-wifi'>WIFI</a>";
    html += "</div>";

    // --- SCRIPT PER AGGIORNAMENTO AUTOMATICO (AUTO-UPDATE SCRIPT) ---
    html += "<script>";
    html += "function updateData() {";
    html += "  fetch('/dashboard_data')";
    html += "    .then(response => response.json())";
    html += "    .then(data => {";
    html += "      document.getElementById('deltaP').innerHTML = data.deltaP;";
    html += "      if (data.slaves) {";
    html += "        data.slaves.forEach(slave => {";
    html += "          const pressEl = document.getElementById('press-' + slave.id);";
    html += "          if (pressEl) pressEl.innerHTML = slave.press;";
    html += "          const tempEl = document.getElementById('temp-' + slave.id);";
    html += "          if (tempEl) tempEl.innerHTML = slave.temp;";
    html += "        });";
    html += "      }";
    // Pianifica la prossima richiesta SOLO dopo aver finito questa (evita sovrapposizioni)
    html += "      setTimeout(updateData, 2000);"; 
    html += "    })";
    html += "    .catch(error => {";
    html += "      console.error('Errore aggiornamento dati:', error);";
    html += "      setTimeout(updateData, 2000);"; // Riprova anche in caso di errore
    html += "    });";
    html += "}";
    // Avvia al caricamento della pagina
    html += "document.addEventListener('DOMContentLoaded', updateData);";
    html += "</script>";

    html += "</div></body></html>";
    return html;
}

// --- GESTIONE WIFI (SCANSIONE E CONNESSIONE) ---
void handleWifiPage() {    
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<link rel='icon' href='" + String(FAVICON_IMG) + "'>";
    html += "<style>body{font-family:'Segoe UI', sans-serif; background-color:#f0f2f5; padding:20px; color:#333;} ";
    html += ".container{max-width:600px; margin:0 auto;}";
    html += ".card{background:white; padding:20px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.1); margin-bottom:20px;}";
    html += "h2, h3{color:#0056b3; margin-top:0;}";
    html += ".net{padding:10px; border-bottom:1px solid #eee; cursor:pointer; display:flex; justify-content:space-between;}";
    html += ".net:hover{background-color:#f9f9f9;}";
    html += "input[type='text'], input[type='password'] {padding:10px; width:100%; margin:5px 0 15px 0; border-radius:5px; border:1px solid #ccc; box-sizing:border-box;}";
    html += "label{font-weight:bold; font-size:0.9em;}";
    html += ".btn{background:#28a745; color:white; border:none; padding:12px; width:100%; border-radius:5px; font-weight:bold; cursor:pointer;}";
    html += ".btn-back{background:#6c757d; margin-top:10px;}";
    html += ".hidden{display:none;}";
    html += "</style>";
    html += "<script>";
    html += "function selNet(ssid) { document.getElementById('ssid').value = ssid; document.getElementById('pass').focus(); }";
    html += "function toggleStatic() { var d = document.getElementById('static_div'); d.classList.toggle('hidden'); }";
    html += "</script>";
    html += "</head><body><div class='container'>";

    // --- 1. STATO CONNESSIONE ---
    if (WiFi.status() == WL_CONNECTED) {
        html += "<div class='card'>";
        html += "<h3>Stato Connessione</h3>";
        html += "<p><b>SSID:</b> " + WiFi.SSID() + "</p>";
        html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
        html += "<p><b>Gateway:</b> " + WiFi.gatewayIP().toString() + "</p>";
        html += "<p><b>Signal:</b> " + String(WiFi.RSSI()) + " dBm</p>";
        html += "</div>";
    }

    // --- 2. LISTA RETI WIFI ---
    html += "<div class='card'>";
    html += "<div style='position:relative;'>";
    html += "<h3>Reti WiFi Rilevate</h3>";
    html += "<a href='/wifi' style='position:absolute; right:0; top:0; font-size:1.5em; text-decoration:none; color:#28a745;' title='Aggiorna'>&#x21bb;</a>";
    html += "</div>";
    html += "<div style='max-height:200px; overflow-y:auto; border:1px solid #ddd; border-radius:5px;'>";
    int n = WiFi.scanNetworks();
    if (n == 0) {
        html += "<div style='padding:10px;'>Nessuna rete trovata.</div>";
    } else {
        for (int i = 0; i < n; ++i) {
            html += "<div class='net' onclick='selNet(\"" + WiFi.SSID(i) + "\")'>";
            html += "<span>" + WiFi.SSID(i) + "</span><span style='color:#666;'>" + String(WiFi.RSSI(i)) + " dBm</span></div>";
        }
    }
    html += "</div></div>";

    // --- FORM CONFIGURAZIONE ---
    html += "<form action='/connect' method='POST'>";
    
    // --- 3. CREDENZIALI & IP STATICO ---
    html += "<div class='card'>";
    html += "<h3>Configurazione Rete</h3>";
    html += "<label>SSID (Nome Rete)</label>";
    html += "<input type='text' name='ssid' id='ssid' placeholder='Seleziona o scrivi...'>";
    html += "<label>Password</label>";
    html += "<input type='password' name='pass' id='pass' placeholder='Password WiFi'>";
    
    // Recupero valori salvati per IP Statico
    memoria.begin("easy", true);
    bool st = memoria.getBool("static_ip", false);
    // Usa isKey per evitare errori "NOT_FOUND" nel log
    String ip = memoria.isKey("ip") ? memoria.getString("ip") : "";
    String sub = memoria.isKey("sub") ? memoria.getString("sub") : "255.255.255.0";
    String gw = memoria.isKey("gw") ? memoria.getString("gw") : "";
    bool apAlways = memoria.getBool("ap_always", false);
    String api = memoria.isKey("api_url") ? memoria.getString("api_url") : "";
    String key = memoria.isKey("apiKey") ? memoria.getString("apiKey") : "";
    memoria.end();

    html += "<div style='margin-bottom:15px;'>";
    html += "<input type='checkbox' name='static_ip' onchange='toggleStatic()' " + String(st ? "checked" : "") + "> Usa Indirizzo IP Statico (Manuale)";
    html += "</div>";

    html += "<div id='static_div' class='" + String(st ? "" : "hidden") + "'>";
    html += "<label>Indirizzo IP</label><input type='text' name='ip' value='" + ip + "' placeholder='es. 192.168.1.100'>";
    html += "<label>Subnet Mask</label><input type='text' name='sub' value='" + sub + "' placeholder='es. 255.255.255.0'>";
    html += "<label>Gateway</label><input type='text' name='gw' value='" + gw + "' placeholder='es. 192.168.1.1'>";
    html += "</div>";
    html += "</div>";

    // --- 4. GESTIONE ACCESS POINT ---
    html += "<div class='card'>";
    html += "<h3>Access Point Interno</h3>";
    html += "<p style='font-size:0.9em; color:#666;'>Di default, l'AP si spegne se connesso al WiFi e si riaccende se disconnesso.</p>";
    html += "<input type='checkbox' name='ap_always' " + String(apAlways ? "checked" : "") + "> <b>Mantieni AP Sempre Attivo</b>";
    html += "</div>";

    // --- 5. CONFIGURAZIONE API ---
    html += "<div class='card'>";
    html += "<h3>Configurazione API Server</h3>";
    html += "<label>URL Endpoint JSON</label>";
    html += "<input type='text' name='api_url' value='" + api + "' placeholder='http://server.com/api/data'>";
    html += "<label>API Key (Codice Sicurezza)</label>";
    html += "<input type='password' name='api_key' value='" + key + "' placeholder='Inserire codice a 64 caratteri'>";
    html += "</div>";

    // TASTI
    html += "<input type='submit' value='SALVA E CONNETTI' class='btn'>";
    html += "</form>";
    
    html += "<a href='/'><button class='btn btn-back'>TORNA ALLA DASHBOARD</button></a>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleConnect() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    bool staticIp = server.hasArg("static_ip");
    String ip = server.arg("ip");
    String sub = server.arg("sub");
    String gw = server.arg("gw");
    
    bool apAlways = server.hasArg("ap_always");
    String apiUrl = server.arg("api_url");
    String apiKey = server.arg("api_key");

    // Salvataggio in NVS
    memoria.begin("easy", false);
    memoria.putString("ssid", ssid);
    memoria.putString("pass", pass);
    memoria.putBool("static_ip", staticIp);
    if(staticIp) { memoria.putString("ip", ip); memoria.putString("sub", sub); memoria.putString("gw", gw); }
    memoria.putBool("ap_always", apAlways);
    memoria.putString("api_url", apiUrl);
    memoria.putString("apiKey", apiKey);
    memoria.end();

    server.send(200, "text/html", "<html><body><h2>Dati salvati!</h2><p>Il Master si riavviera' per connettersi a " + ssid + ".</p></body></html>");
    delay(2000);
    ESP.restart();
}

// --- INVIO DATI JSON ---
void handleData() {
    String json = "{";
    json += "\"internet\":" + String(statoInternet) + ",";
    json += "\"scansione\":" + String(scansioneInCorso ? 1 : 0);
    json += "}";
    server.send(200, "application/json", json);
}

// --- INVIO DATI JSON PER DASHBOARD ---
void handleDashboardData() {
    // Calcolo Delta Pressione (Pressure Delta Calculation)
    float pressG1 = 0.0, pressG2 = 0.0;
    bool foundG1 = false, foundG2 = false;

    for(int i=1; i<=100; i++) {
        if(listaPerifericheAttive[i]) {
            if (databaseSlave[i].grp == 1 && !foundG1) {
                pressG1 = databaseSlave[i].p;
                foundG1 = true;
            }
            else if (databaseSlave[i].grp == 2 && !foundG2) {
                pressG2 = databaseSlave[i].p;
                foundG2 = true;
            }
        }
    }

    String deltaPStr = (foundG1 && foundG2) ? String(pressG1 - pressG2, 0) + " Pa" : "In attesa dati";
    
    // Aggiornamento variabile globale per calibrazione
    if (foundG1 && foundG2) {
        currentDeltaP = pressG1 - pressG2;
    }

    // Costruzione JSON (JSON Construction)
    String json = "{";
    json += "\"deltaP\":\"" + deltaPStr + "\",";
    json += "\"slaves\":[";

    bool firstSlave = true;
    for (int i = 1; i <= 100; i++) {
        if (listaPerifericheAttive[i]) {
            if (!firstSlave) {
                json += ",";
            }
            json += "{";
            json += "\"id\":" + String(i) + ",";
            json += "\"press\":\"" + String(databaseSlave[i].p, 0) + " Pa\",";
            json += "\"ver\":\"" + String(databaseSlave[i].version) + "\",";
            json += "\"temp\":\"" + String(databaseSlave[i].t, 1) + " &deg;C\"";
            json += "}";
            firstSlave = false;
        }
    }

    json += "]}";
    // Chiude la connessione subito dopo l'invio per evitare "Connection reset by peer"
    server.sendHeader("Connection", "close");
    server.send(200, "application/json", json);
}

// --- SETUP SERVER ---
void setupMasterWiFi() {
    // Prova a connettersi come Client se c'è un SSID salvato
    memoria.begin("easy", true);
    String s = memoria.getString("ssid", "");
    String p = memoria.getString("pass", "");
    
    bool staticIp = memoria.getBool("static_ip", false);
    String ipStr = memoria.isKey("ip") ? memoria.getString("ip") : "";
    String subStr = memoria.isKey("sub") ? memoria.getString("sub") : "";
    String gwStr = memoria.isKey("gw") ? memoria.getString("gw") : "";
    String k = memoria.isKey("apiKey") ? memoria.getString("apiKey") : "";
    k.toCharArray(config.apiKey, 65);
    
    memoria.end();

    if (s != "") {
        // Configurazione IP Statico se richiesto
        if (staticIp && ipStr != "") {
            IPAddress ip, sub, gw;
            if(ip.fromString(ipStr) && sub.fromString(subStr) && gw.fromString(gwStr)) {
                WiFi.config(ip, gw, sub);
                Serial.println("[WIFI] Configurazione IP Statico applicata.");
            }
        }

        WiFi.begin(s.c_str(), p.c_str());
        Serial.print("[WIFI] Connessione a " + s);
    }

    // Avvio Iniziale AP (Sempre attivo all'avvio per sicurezza, poi gestito dal loop)
    // Se non c'è rete configurata, resta attivo.
    WiFi.softAP("AntraluxRewamping", NULL);
    dnsServer.start(53, "*", WiFi.softAPIP());
    apEnabled = true;
    Serial.println("[WIFI] Access Point Aperto: AntraluxRewamping");

    // Avvio mDNS
    if (MDNS.begin("antraluxrewamping")) {
        Serial.println("[WIFI] mDNS avviato: http://antraluxrewamping.local");
    }

    // Gestione Favicon per evitare errori nel log
    server.on("/favicon.ico", []() { server.send(204); });

    // Gestione Root (Home Page)
    server.on("/", []() {
        server.send(200, "text/html", getDashboardHTML());
    });

    // Setup Calibrazione
    setupCalibration();

    server.on("/dashboard_data", handleDashboardData); // Nuovo endpoint per i dati (New endpoint for data)
    server.on("/data", handleData);
    server.on("/wifi", handleWifiPage);
    server.on("/connect", HTTP_POST, handleConnect);
    
    // Captive Portal (Android/Windows/iOS)
    server.on("/generate_204", []() { server.send(200, "text/html", getDashboardHTML()); });
    server.on("/fwlink", []() { server.send(200, "text/html", getDashboardHTML()); });
    
    server.onNotFound([]() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    });

    server.begin();
}

void gestisciWebEWiFi() {
    dnsServer.processNextRequest();
    server.handleClient();

    // --- LOGICA AUTOMATICA AP E RICONNESSIONE ---
    unsigned long now = millis();
    if (now - lastWifiCheck > 5000) { // Controllo ogni 5 secondi
        lastWifiCheck = now;

        memoria.begin("easy", true);
        bool apAlwaysOn = memoria.getBool("ap_always", false);
        memoria.end();

        if (WiFi.status() == WL_CONNECTED) {
            statoInternet = 2; // Connesso
            wifiRetryCount = 0; // Resetta il contatore se la connessione ha successo

            // Se connesso, AP è attivo e NON deve essere sempre attivo -> Spegni AP
            if (!apAlwaysOn && apEnabled) {
                 WiFi.softAPdisconnect(true);
                 apEnabled = false;
                 Serial.println("[WIFI] Connesso a Internet. AP Disattivato (Auto).");
            }
            // Se connesso, l'utente vuole l'AP sempre attivo ma è spento -> Accendi AP
            else if (apAlwaysOn && !apEnabled) {
                 WiFi.softAP("AntraluxRewamping", NULL);
                 apEnabled = true;
                 Serial.println("[WIFI] AP Riattivato (Impostazione Sempre Attivo).");
            }
        } else {
            statoInternet = 0; // Disconnesso

            // Se la modalità "Sempre Attivo" è disabilitata, gestiamo i tentativi
            if (!apAlwaysOn) {
                wifiRetryCount++;
                Serial.printf("[WIFI] Connessione persa. Tentativo di riconnessione %d/10...\n", wifiRetryCount);

                // Se superiamo i 10 tentativi e l'AP è spento, lo attiviamo come fallback
                if (wifiRetryCount > 10 && !apEnabled) {
                    WiFi.softAP("AntraluxRewamping", NULL);
                    apEnabled = true;
                    Serial.println("[WIFI] Riconnessione fallita. AP Riattivato (Recovery).");
                }
            }
        }
    }
}
