/* Standard library include. */
#include <stdint.h>
#include <string.h>

#include "mqtt_common.h"

/* Maximum size of the Job Document */
#define RENEW_JOB_DOC_SIZE 512U
#define CERTIFICATE_LENGTH 1500


typedef enum RenewState
{
    RenewStateInit = 0,
    RenewStateReady,
    RenewStateProcessingJob,
    RenewStateClientCertRenewal,
    RenewStateRenewingClientCert,
    RenewStateWaitingSignedCertificate,
    RenewStateProcessingSignedCertificate,
    RenewStateRevokingOldCertificate,
    RenewStateMax
} RenewState_t;

typedef enum {
    RenewEventStart = 0,
    RenewEventReady,
    RenewEventReceivedJobDocument,
    RenewEventClientCertificateRenewal,
    RenewEventReceivedSignedCertificate,
    RenewEventRejectedCertificateSigningRequest,
    RenewEventWaitSignedCertificate,
    RenewEventRejectedSignedCertificate,
    RenewEventRevokeOldCertificate,
    RenewEventAcceptedOldCertificateRevoke,
    RenewEventRejectedOldCertificateRevoke,
    RenewEventMax
} RenewEvent_t;

typedef struct RenewDataEvent
{
    uint8_t data[CERTIFICATE_LENGTH];
    size_t dataLength;                
    bool bufferUsed;                     
} RenewDataEvent_t;

typedef struct RenewEventMsg
{
    RenewDataEvent_t * dataEvent; /*!< Data Event message. */
    JobEventData_t * jobEvent; /*!< Job Event message. */
    RenewEvent_t eventId;          /*!< Identifier for the event. */
} RenewEventMsg_t;

typedef struct Operation
{
    char * operation;
    size_t operationLength;
    char * certName;
    size_t certNameLength;
} Operation_t;

void renewAgentTask( void * parameters);
void UpdateStatusRenew(BaseType_t status);
