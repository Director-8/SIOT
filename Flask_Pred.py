import boto3
import joblib
import os
import tempfile
from collections import defaultdict
import pandas as pd
from influxdb_client import InfluxDBClient
from flask import Flask, jsonify

# S3 and InfluxDB Configuration
S3_BUCKET = "siotweather"
MODEL_KEY = "decision_tree_model.pkl"
INFLUX_URL = "https://eu-central-1-1.aws.cloud2.influxdata.com/"
INFLUX_TOKEN = "TspylIdlK3FlJyup6cGvUGvF7gtRIy-C1L_63UhZY7IDOlwe0T3z9yZi8jrrS0MB1_HACCYQPZ4UIh0JWl7tbQ=="
INFLUX_ORG = "IoT"
INFLUX_BUCKET = "Weather"

# Feature list used during model training
FEATURES = ['blue', 'clouds', 'green', 'humidity', 'luminosity',
            'pressure', 'rainfall', 'red', 'temperature', 'visibility']

# Initialize S3 client and download the model
s3 = boto3.client("s3")
temp_dir = tempfile.gettempdir()
model_path = os.path.join(temp_dir, "decision_tree_model.pkl")
s3.download_file(S3_BUCKET, MODEL_KEY, model_path)

# Load the model
model = joblib.load(model_path)
print("Model loaded successfully.")

def get_latest_sensor_data():
    """Fetch the most recent 10 entries from InfluxDB and reshape the data."""
    client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    
    query = f'''
        from(bucket: "{INFLUX_BUCKET}")
        |> range(start: -30d)
        |> sort(columns: ["_time"], desc: true)
        |> limit(n: 100)
    '''
    
    tables = client.query_api().query(query)
    data_by_time = defaultdict(dict)
    
    for table in tables:
        for record in table.records:
            timestamp = record.values.get('_time')
            field = record.values.get('_field')
            value = record.values.get('_value')
            data_by_time[timestamp][field] = value

    results = []
    for timestamp, fields in data_by_time.items():
        fields['_time'] = timestamp
        results.append(fields)

    df = pd.DataFrame(results)
    df = df[['_time'] + FEATURES].dropna()
    df = df.sort_values(by='_time', ascending=False).head(10).drop(columns=['_time'])
    
    return df

# Flask app setup
app = Flask(__name__)

@app.route('/predict', methods=['GET'])
def predict():
    """Endpoint to get the latest prediction."""
    sensor_data_df = get_latest_sensor_data()
    
    if sensor_data_df.empty or len(sensor_data_df) < 10:
        return jsonify({"error": "Not enough data available from InfluxDB"}), 500
    
    predictions = model.predict(sensor_data_df)
    result = int(predictions[0])  # Get the first prediction (0 or 1)
    return jsonify({"prediction": result})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)