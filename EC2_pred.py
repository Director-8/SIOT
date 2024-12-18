import boto3
import joblib
import os
import tempfile
import time
from collections import defaultdict
import pandas as pd
import numpy as np
from influxdb_client import InfluxDBClient

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

# Initialize S3 client
s3 = boto3.client("s3")

# Download model from S3 to a temporary directory
temp_dir = tempfile.gettempdir()
model_path = os.path.join(temp_dir, "decision_tree_model.pkl")
s3.download_file(S3_BUCKET, MODEL_KEY, model_path)

# Load the model
model = joblib.load(model_path)
print("Model loaded successfully.")

def get_latest_sensor_data():
    """Fetch the most recent 10 entries from the past 30 days from InfluxDB and reshape the data."""
    client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    
    query = f'''
        from(bucket: "{INFLUX_BUCKET}")
        |> range(start: -30d)
        |> sort(columns: ["_time"], desc: true)
        |> limit(n: 100)
    '''
    
    tables = client.query_api().query(query)
    
    # Use a defaultdict to group data by timestamp
    data_by_time = defaultdict(dict)
    
    for table in tables:
        for record in table.records:
            timestamp = record.values.get('_time')
            field = record.values.get('_field')
            value = record.values.get('_value')
            data_by_time[timestamp][field] = value

    # Convert the grouped data into a DataFrame
    results = []
    for timestamp, fields in data_by_time.items():
        fields['_time'] = timestamp
        results.append(fields)

    # Create DataFrame
    df = pd.DataFrame(results)
    
    # Filter the columns to match the expected features
    feature_columns = FEATURES
    
    # Drop rows with missing values and keep only relevant features
    df = df[['_time'] + feature_columns].dropna()

    # Sort by time and select the 10 most recent entries
    df = df.sort_values(by='_time', ascending=False).head(10).drop(columns=['_time'])
    
    print(f"Fetched data:\n{df}")
    
    return df

def run_prediction_loop():
    """Continuously fetch data and make predictions every minute."""
    try:
        while True:
            # Get the latest data
            sensor_data_df = get_latest_sensor_data()
            
            if sensor_data_df.empty or len(sensor_data_df) < 10:
                print("Not enough data available from InfluxDB.")
            else:
                # Make predictions
                predictions = model.predict(sensor_data_df)
                
                # Output the predictions
                print(f"Predictions: {predictions.tolist()}")
            
            # Wait for 1 minute before the next prediction
            time.sleep(60)

    except KeyboardInterrupt:
        print("Prediction loop stopped by user.")

# Start the prediction loop
run_prediction_loop()