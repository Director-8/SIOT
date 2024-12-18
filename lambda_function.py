import boto3
import joblib
import os
import tempfile
import json
from influxdb_client import InfluxDBClient

# S3 and InfluxDB Configuration
S3_BUCKET = "siotweather"
MODEL_KEY = "decision_tree_model.pkl"
INFLUX_URL = "https://eu-central-1-1.aws.cloud2.influxdata.com/"
INFLUX_TOKEN = "TspylIdlK3FlJyup6cGvUGvF7gtRIy-C1L_63UhZY7IDOlwe0T3z9yZi8jrrS0MB1_HACCYQPZ4UIh0JWl7tbQ=="
INFLUX_ORG = "IoT"
INFLUX_BUCKET = "Weather"

# Initialize S3 client
s3 = boto3.client("s3")

# Download model from S3 to a temporary directory
temp_dir = tempfile.gettempdir()
model_path = os.path.join(temp_dir, "decision_tree_model.pkl")
s3.download_file(S3_BUCKET, MODEL_KEY, model_path)

# Load the model
model = joblib.load(model_path)

def get_latest_sensor_data():
    """Fetch the newest 10 entries from InfluxDB."""
    client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    
    query = f'''
        from(bucket: "{INFLUX_BUCKET}")
        |> range(start: -1h)
        |> sort(columns: ["_time"], desc: true)
        |> limit(n: 10)
    '''
    
    tables = client.query_api().query(query)
    
    results = []
    for table in tables:
        for record in table.records:
            results.append([
                record.values.get('blue'),
                record.values.get('clouds'),
                record.values.get('green'),
                record.values.get('humidity'),
                record.values.get('luminosity'),
                record.values.get('pressure'),
                record.values.get('rainfall'),
                record.values.get('red'),
                record.values.get('temperature'),
                record.values.get('visibility')
            ])
    
    return results


def lambda_handler(event, context):
    try:
        # Get the latest 10 entries from InfluxDB
        sensor_data_list = get_latest_sensor_data()
        
        if not sensor_data_list or len(sensor_data_list) < 10:
            return {
                "statusCode": 500,
                "body": json.dumps({"error": "Not enough data available from InfluxDB"})
            }

        # Convert to a NumPy array for prediction
        sensor_data_array = np.array(sensor_data_list)

        # Make predictions for the 10 newest entries
        predictions = model.predict(sensor_data_array)

        return {
            "statusCode": 200,
            "body": json.dumps({"predictions": predictions.tolist()})
        }

    except Exception as e:
        return {
            "statusCode": 500,
            "body": json.dumps({"error": str(e)})
        }