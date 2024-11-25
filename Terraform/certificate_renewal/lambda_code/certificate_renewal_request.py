import boto3
import os
import json
from datetime import datetime

client = boto3.client('iot')

def lambda_handler(event, context):

    body = json.loads(event['body'])
    
    if 'jobId' not in body or 'groupName' not in body:
        return {
            'statusCode': 400,
            'body': json.dumps("Missing 'jobId' or 'groupName' in the event body.")
        }
        
    job_id_prefix = body['jobId']
    group_name = body['groupName']

    aws_account_id = os.environ['AWS_ACCOUNT_ID']

    # Build the ARN of the device group (thing group)
    target_arn = f'arn:aws:iot:us-east-1:{aws_account_id}:thinggroup/{group_name}'

    document_source = os.environ['JOB_DOCUMENT_SOURCE']

    # Generate the jobId in the format: jobIdprefix-groupName-date
    current_date = datetime.now().strftime('%Y-%m-%d')
    job_id = f"{job_id_prefix}-{group_name}-{current_date}"
    
    try:
        
        response = client.create_job(
            jobId=job_id,
            targets=[target_arn], 
            documentSource=document_source,  
            targetSelection='SNAPSHOT',  
            jobExecutionsRolloutConfig={
                'maximumPerMinute': 1  
            },
            timeoutConfig={
                'inProgressTimeoutInMinutes': 5
            }
        )
        
        return {
            'statusCode': 200,
            'body': json.dumps(f"Job created successfully: {response}")
        }
    
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Error creating the job: {str(e)}")
        }


