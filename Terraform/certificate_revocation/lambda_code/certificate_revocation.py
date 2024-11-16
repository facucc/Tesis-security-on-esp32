import boto3
import json
import os

client = boto3.client('iot')
data_client = boto3.client('iot-data')

def publish_message(topic, thingName, response):

    topic_replace = os.environ.get(topic)

    if not topic_replace:
        raise ValueError("Environment variable 'TOPIC' is not set")

    response_topic = topic_replace.replace('thingName', thingName)
    
    print('topic:' + response_topic)
    print('response:' + json.dumps(response))

    data_client.publish(
        topic=response_topic,
        qos=0,
        payload=json.dumps(response)
    )

def lambda_handler(event, context):

    try:
        if 'thingName' not in event or 'certificateId' not in event:
            raise ValueError("Missing 'thingName' or 'certificateId' in event")

        thingName = event["thingName"]
        certificateId = event["certificateId"]
        
        certsResponse = client.list_thing_principals(
            thingName = thingName
        )

        #revoke and delete all certs associated with this thing except for the new one
        for certARN in certsResponse["principals"]:
            #get cert id
            removeCertificateId = certARN.split("/")

            if removeCertificateId[1] != certificateId:

                #revoke the certificate
                updateCertResponse = client.update_certificate(
                    certificateId = removeCertificateId[1],
                    newStatus = 'REVOKED'
                )
                print("Revoked cert: " + removeCertificateId[1])

                #list the policies for the cert so they can be detached
                policyResponse = client.list_attached_policies(
                    target=certARN,
                    recursive=False
                )

                #remove the attachement
                policyName = policyResponse['policies'][0]['policyName']

                response = client.detach_principal_policy(
                    policyName=policyName,
                    principal=certARN
                )
                print("Detached policy: " + policyName)

                #detach the cert from the thing
                response = client.detach_thing_principal(
                    thingName=thingName,
                    principal=certARN
                )
                #delete the certificate
                response = client.delete_certificate(
                    certificateId=removeCertificateId[1],
                    forceDelete=False
                )
                print("Delete cert: " + removeCertificateId[1])

        msg = {"Status": "accepted"}

        publish_message('ACCEPTED_TOPIC', thingName, msg)
        
        return {
            'statusCode': 200,
            'body': json.dumps('Revoke Certificate Ok')
        }
    except client.exceptions.InvalidRequestException as e:
        print(f"InvalidRequestException: {str(e)}")

        msg = {
            "statusCode": 400,
            "error": e.response.get('message', 'No message available')
        }
        
        publish_message('REJECTED_TOPIC', thingName, msg)      
        
        return {
            'statusCode': 400,
            'body': json.dumps(msg)
        }
