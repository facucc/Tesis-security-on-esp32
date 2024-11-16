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
        if 'thingName' not in event or 'certificateSigningRequest' not in event:
            raise ValueError("Missing 'thingName' or 'certificateSigningRequest' in event")
            
        print(event)
        
        thingName = event["thingName"]
        csr = event['certificateSigningRequest']
        
        print('csr', csr)
        print('ThingName:', thingName)
        
        csr = csr.replace('\n', '')

        print('csr', csr)
        print('ThingName:', thingName)
        
        certResponse = client.create_certificate_from_csr(
            certificateSigningRequest = csr,
            setAsActive=True
        )

        thingResponse = client.list_thing_principals(
            maxResults=1,
            thingName=thingName
        )
        if not thingResponse['principals']:
            raise ValueError(f"No principals found for Thing '{thingName}'")

        policyResponse = client.list_attached_policies(
            target=','.join(thingResponse['principals'])
        )

        if not policyResponse['policies']:
            raise ValueError(f"No policies attached to Thing '{thingName}'")

        print(policyResponse['policies'][0]['policyName'])

        thingAttachResponse = client.attach_thing_principal(
            thingName=thingName,
            principal=certResponse['certificateArn']
        )

        policyAttachresponse = client.attach_policy(
            policyName=policyResponse['policies'][0]['policyName'],
            target=certResponse['certificateArn']
        )

        msg = {
            "certificateId": certResponse['certificateId'],
            "certificatePem": certResponse['certificatePem']
        }

        publish_message('ACCEPTED_TOPIC', thingName, msg)

        return {
            'statusCode': 200,
            'body': json.dumps('Renovation Ok')
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