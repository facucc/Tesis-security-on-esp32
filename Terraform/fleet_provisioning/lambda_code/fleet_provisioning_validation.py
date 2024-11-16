import json
import os
import boto3
from botocore.exceptions import ClientError

dynamodb = boto3.client('dynamodb')

def lambda_handler(event, context):
    try:
        mac_address = event['parameters']['MacAddress']
        
    except KeyError:
        return {
            "allowProvisioning": False,
            "errorMessage": "Missing MacAddress parameter"
        }
    
    print(f'Validating MAC address: {mac_address}')
    
    if validate_mac_address(mac_address):
        print("MAC address is valid. Allowing provisioning.")
        
        if update_onboarding_status(mac_address, False):
            return {
                "allowProvisioning": True,
                "parameterOverrides": {
                    "ThingName": mac_address
                }
            }
        else:
            return {
                "allowProvisioning": False,
                "errorMessage": "Failed to update OnBoarding status"
            }
    else:
        print("Device does not exist.")
        
        return {
            "allowProvisioning": False,
            "errorMessage": "Device does not exist."
        }

def validate_mac_address(mac_address):
    try:
        table_name = os.environ.get('DynamoDBTable')
        if not table_name:
            print("DynamoDBTable environment variable not set.")
            return False

        response = dynamodb.get_item(
            TableName=table_name,
            Key={
                'MacAddress': {'S': mac_address},
                'dev': {'S': 'dev'}
            }
        )
        
        print(f"DynamoDB response: {response}")
        
        if 'Item' in response:
            onboarding_status = response['Item'].get('OnBoarding', {}).get('BOOL', False)
            if onboarding_status:
                print("The device exists and is ready to register.")
                return True
            else:
                print("Device exists but is already registered.")
                return False
        else:
            return False

    except ClientError as e:
        print(f"DynamoDB ClientError: {e.response['Error']['Message']}")
        return False
    except Exception as e:
        print(f"Unexpected error: {str(e)}")
        return False

def update_onboarding_status(mac_address, status):
    try:
        table_name = os.environ.get('DynamoDBTable')
        if not table_name:
            print("DynamoDBTable environment variable not set.")
            return False

        # Update the OnBoarding status
        response = dynamodb.update_item(
            TableName=table_name,
            Key={
                'MacAddress': {'S': mac_address},
                'dev': {'S': 'dev'}
            },
            UpdateExpression="SET OnBoarding = :status",
            ExpressionAttributeValues={
                ':status': {'BOOL': status}
            },
            ReturnValues="UPDATED_NEW"
        )
        
        print(f"Update response: {response}")
        return True

    except ClientError as e:
        print(f"DynamoDB ClientError: {e.response['Error']['Message']}")
        return False
    except Exception as e:
        print(f"Unexpected error: {str(e)}")
        return False

