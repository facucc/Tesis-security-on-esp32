/* Standard library include. */
#include <stdint.h>
#include <string.h>

#include "mqtt_common.h"

#define RENEW_JOB_DOC_SIZE  512U
#define CERTIFICATE_LENGTH  1500
#define OLD_REVOKE_MSG_SIZE 256
#define TOPIC_FILTER_LENGTH 100

/*
 * Description: The states of the certificate renewal state machine.
 * States:
 *      - CertRenewStateInit: Initial state of the renewal agent.
 *      - CertRenewStateReady: Ready to process a renewal job.
 *      - CertRenewStateProcessingJob: Processing a renewal job document.
 *      - CertRenewStateClientCertRenewal: Initiating the client certificate renewal process.
 *      - CertRenewStateRenewingClientCert: Performing the client certificate renewal operation.
 *      - CertRenewStateWaitingSignedCertificate: Awaiting a signed certificate from AWS.
 *      - CertRenewStateProcessingSignedCertificate: Handling the received signed certificate.
 *      - CertRenewStateRevokingOldCertificate: Revoking the previous client certificate.
 *      - CertRenewStateMax: Represents the total number of states.
 */
typedef enum CertRenewState {
    CertRenewStateInit = 0,
    CertRenewStateReady,
    CertRenewStateProcessingJob,
    CertRenewStateClientCertRenewal,
    CertRenewStateRenewingClientCert,
    CertRenewStateWaitingSignedCertificate,
    CertRenewStateProcessingSignedCertificate,
    CertRenewStateRevokingOldCertificate,
    CertRenewStateMax
} CertRenewState_t;

/*
 * Description: The events that trigger transitions in the certificate renewal state machine.
 * Events:
 *      - CertRenewEventStart: Start the renewal process.
 *      - CertRenewEventReady: Indicates the agent is ready to process a job.
 *      - CertRenewEventReceivedJobDocument: Renewal job document has been received.
 *      - CertRenewEventClientCertificateRenewal: Begins the client certificate renewal process.
 *      - CertRenewEventReceivedSignedCertificate: A signed certificate has been received from AWS.
 *      - CertRenewEventRejectedCertificateSigningRequest: CSR was rejected by the CA.
 *      - CertRenewEventWaitSignedCertificate: Waiting for the signed certificate.
 *      - CertRenewEventRejectedSignedCertificate: Signed certificate was rejected.
 *      - CertRenewEventRevokeOldCertificate: Begins the revocation of the old certificate.
 *      - CertRenewEventAcceptedOldCertificateRevoke: Old certificate revocation was accepted.
 *      - CertRenewEventRejectedOldCertificateRevoke: Old certificate revocation was rejected.
 *      - CertRenewEventMax: Represents the total number of events.
 */
typedef enum {
    CertRenewEventStart = 0,
    CertRenewEventReady,
    CertRenewEventReceivedJobDocument,
    CertRenewEventClientCertificateRenewal,
    CertRenewEventReceivedSignedCertificate,
    CertRenewEventRejectedCertificateSigningRequest,
    CertRenewEventWaitSignedCertificate,
    CertRenewEventRejectedSignedCertificate,
    CertRenewEventRevokeOldCertificate,
    CertRenewEventAcceptedOldCertificateRevoke,
    CertRenewEventRejectedOldCertificateRevoke,
    CertRenewEventMax
} CertRenewEvent_t;

typedef struct CertRenewDataEvent {
    uint8_t data[CERTIFICATE_LENGTH]; /* Buffer to store the certificate */
    size_t dataLength;
    bool bufferUsed;
} CertRenewDataEvent_t;

typedef struct CertRenewEventMsg {
    CertRenewDataEvent_t* dataEvent;
    JobEventData_t* jobEvent;
    CertRenewEvent_t eventId;
} CertRenewEventMsg_t;

typedef struct Operation {
    char* operation;
    size_t operationLength;
    char* certName;
    size_t certNameLength;
} Operation_t;

/*
 * Entry point for executing the certificate renewal agent.
 * This function manages the renewal process by implementing a
 * state machine that processes events related to certificate
 * renewal, including receiving job documents, handling CSR
 * operations, and processing signed certificates.
 */
void renewAgentTask(void* parameters);
void UpdateStatusRenew(BaseType_t status);
